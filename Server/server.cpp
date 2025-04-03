#include "crow_all.h"                          //microframework for webservices
#include <bsoncxx/json.hpp>                    //mongo c++ (BSON [mongo's storage format] to JSON)
#include <bsoncxx/builder/stream/document.hpp> //mongo c++ (helps to build BSON)
#include <mongocxx/client.hpp>                 //mongo c++ (server connector)
#include <mongocxx/instance.hpp>               //mongo c++ (initializes mongo driver)
#include <mongocxx/uri.hpp>                    //mongo c++ (handles connection strings)
#include <jwt-cpp/jwt.h>                       //JWT creation and validation
#include <chrono>                              //time functions
#include <cstdlib>                             //general utilities
#include <string>                              //string class
#include <iostream>                            //input/output strings (logging)
#include <mutex>                               //for mutex
#include <unordered_map>                       //key-value lookups
#include <semaphore>                           //for semaphores
#include <vector>                              //c++ arrays
#include <fstream>                             //reading/writing files

/*
Full End-Point Map:

Group 0: Connectivity and Database Info Endpoints
  0.1 - Connectivity Test
      GET "/"
      Returns a simple message confirming the backend server is running.
  0.2 - Database Structure
      GET "/api/db-structure"
      Returns the list of collection names in the database.

Group 1: Account Endpoints [FELIX]
  1.1 - Create Account
      POST "/api/create-account"
      Validates input, checks for existing account, and inserts a new account.
  1.2 - Login
      POST "/api/login"
      Validates credentials and returns user details with a JWT token.
      Deadlock Handling: Uses a std::timed_mutex with a timeout to avoid lock acquisition stalling indefinitely.

Group 2: Surf Location Endpoints [MARK]
  2.1 - Insert Surf Location
      POST "/api/protected/insert-surf-location"
      Inserts a new surf location document into the database.
  2.2 - Surf Locations (Summaries)
      GET "/api/surf-locations"
      Retrieves surf location summaries with optional country and location filters.
      Uses semahpore
  2.3 - Location Details (Granular & Posts)
      GET "/api/location-details"
      Retrieves detailed information for a location along with its associated posts.
      Uses semahpore

Group 3: Post Endpoints [CG]
  3.1 - Create Post
      POST "/api/protected/create-post"
      Inserts a new post document with initial like and comment counts set to zero.
      Explicit Thread Scheduling: Uses std::async(std::launch::async, ...) to schedule the database insertion on a separate thread.

Group 4: Comment Endpoints [BILLY]
  4.1 - Create Comment
      POST "/api/protected/create-comment"
      Inserts a new comment for a post.
  4.2 - Get Post Comments
      GET "/api/post-comments"
      Retrieves all comments (with like counts) for a given post.
      Explicit Thread Scheduling: Also uses std::async to retrieve and process comments in a separate thread.
  4.3 - Like Comment
      POST "/api/protected/like-comment"
      Increments the like count for a comment.
*/

// load environment variables from .env file
void loadDotEnv(const std::string &path)
{
    // attempt to open file
    std::ifstream file(path);

    // if it doesn't open
    if (!file.is_open())
    {
        std::cerr << "Warning: Could not open .env file at " << path << std::endl;
        return;
    }

    // setup line variable
    std::string line;

    // process file line by line
    while (std::getline(file, line))
    {
        // first non-whitespace character
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos)
            continue; // skip empty lines

        // last non-whitespace character
        size_t end = line.find_last_not_of(" \t");

        // trim whitespace from the beginning and end of a line
        line = line.substr(start, end - start + 1);

        // skip empty or comment lines
        if (line.empty() || line[0] == '#')
            continue;

        // find = that seperates key and value
        size_t delim_pos = line.find('=');
        if (delim_pos == std::string::npos)
            continue; // skip liunes without =

        // extract key and value
        std::string key = line.substr(0, delim_pos);
        std::string value = line.substr(delim_pos + 1);

        // remove surrounding quotes if present
        if (!value.empty() && value.front() == '"' && value.back() == '"')
        {
            value = value.substr(1, value.size() - 2);
        }

        // set environment variable (1 to overwrite if already exists)
        setenv(key.c_str(), value.c_str(), 1);
    }

    // close the file
    file.close();
}

// -----------------------------------------------------------------------------
// CORSMiddleware: Attaches CORS headers & handles OPTIONS requests
// -----------------------------------------------------------------------------
struct CORSMiddleware
{
    struct context
    {
    };

    void before_handle(crow::request &req, crow::response &res, context &ctx)
    {
        // If the request is a preflight OPTIONS request, respond with 200 OK.
        if (req.method == crow::HTTPMethod::OPTIONS)
        {
            res.code = 200;
            res.add_header("Access-Control-Allow-Origin", "*");
            res.add_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            // Include "Origin" if any fetch call sets it manually.
            res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization, Accept, Origin");
            res.end();
            return;
        }
    }

    void after_handle(crow::request &req, crow::response &res, context &ctx)
    {
        // Ensure that all responses have the CORS headers.
        res.add_header("Access-Control-Allow-Origin", "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization, Accept, Origin");
    }
};

// -----------------------------------------------------------------------------
// JWT Utility Functions
// -----------------------------------------------------------------------------
std::string generate_jwt(const std::string &user_id, const std::string &username, const std::string &jwt_secret)
{
    auto token = jwt::create()
                     .set_issuer("EddieAikau")
                     .set_type("JWS")
                     .set_subject(user_id)
                     .set_audience("EddieAikauApp")
                     .set_payload_claim("username", jwt::claim(username))
                     .set_expires_at(std::chrono::system_clock::now() + std::chrono::hours(24))
                     .sign(jwt::algorithm::hs256{jwt_secret});

    std::cout << "Generated JWT token for user: " << username << " with ID: " << user_id << std::endl;
    return token;
}

bool verify_jwt(const std::string &token, std::string &user_id, std::string &username, const std::string &jwt_secret)
{
    try
    {
        std::cout << "Verifying JWT token..." << std::endl;
        auto decoded = jwt::decode(token);

        // Print out some debug info
        std::cout << "Token issuer: " << decoded.get_issuer() << std::endl;
        std::cout << "Token subject: " << decoded.get_subject() << std::endl;

        auto verifier = jwt::verify()
                            .allow_algorithm(jwt::algorithm::hs256{jwt_secret})
                            .with_issuer("EddieAikau");
        verifier.verify(decoded);

        user_id = decoded.get_subject();
        username = decoded.get_payload_claim("username").as_string();

        std::cout << "JWT verification successful. User ID: " << user_id << ", Username: " << username << std::endl;
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "JWT verification error: " << e.what() << std::endl;
        return false;
    }
}

// -----------------------------------------------------------------------------
// JWTMiddleware: Protects routes with "/api/protected"
// -----------------------------------------------------------------------------
struct JWTMiddleware
{
    JWTMiddleware(const std::string &secret) : jwt_secret(secret) {}

    std::string jwt_secret;

    struct context
    {
        std::string user_id;
        std::string username;
    };

    void before_handle(crow::request &req, crow::response &res, context &ctx)
    {
        if (req.url.find("/api/protected") != std::string::npos)
        {
            std::cout << "Protected route accessed: " << req.url << std::endl;

            auto auth_header = req.get_header_value("Authorization");
            if (auth_header.empty() || auth_header.find("Bearer ") != 0)
            {
                std::cout << "Authorization header missing or invalid: " << auth_header << std::endl;
                res.code = 401;
                res.write("Unauthorized: Missing or invalid token");
                res.end();
                return;
            }

            std::string token = auth_header.substr(7);
            std::cout << "Token extracted from header: " << (token.length() > 10 ? token.substr(0, 10) + "..." : token) << std::endl;

            if (!verify_jwt(token, ctx.user_id, ctx.username, jwt_secret))
            {
                std::cout << "JWT verification failed" << std::endl;
                res.code = 401;
                res.write("Unauthorized: Token verification failed");
                res.end();
                return;
            }

            // Add user info to request for handlers to use
            req.add_header("X-User-ID", ctx.user_id);
            req.add_header("X-Username", ctx.username);
            std::cout << "JWT verification successful, added user headers" << std::endl;
        }
    }

    void after_handle(crow::request &req, crow::response &res, context &ctx) {}
};

std::mutex surf_location_mutex; // Global mutex for surf location operations

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main()
{
    loadDotEnv(".env");

    // Move JWT secret initialization here, after .env is loaded
    const char *jwt_secret_env = std::getenv("JWT_SECRET");
    std::string jwt_secret = jwt_secret_env ? jwt_secret_env : "fallback_jwt_secret";

    const char *mongo_uri_env = std::getenv("MONGO_URI");
    std::string mongo_uri = mongo_uri_env ? mongo_uri_env : "mongodb://localhost:27017";

    const char *db_name_env = std::getenv("DATABASE");
    std::string db_name = db_name_env ? db_name_env : "defaultDatabase";

    const char *port_env = std::getenv("PORT");
    int port = port_env ? std::stoi(port_env) : 3000;

    std::cout << "Using Mongo URI: " << mongo_uri << "\n";
    std::cout << "Using Database: " << db_name << "\n";
    std::cout << "Using Port: " << port << "\n";
    std::cout << "JWT Secret: " << (jwt_secret_env ? "Loaded from ENV" : "Fallback used!") << "\n";

    mongocxx::instance instance{};
    mongocxx::client client{mongocxx::uri{mongo_uri}};
    auto db = client[db_name];

    auto accounts_collection = db["Accounts"];
    std::counting_semaphore<3> search_semaphore(3);
    std::timed_mutex session_mutex;
    std::unordered_map<std::string, int> active_sessions;

    crow::App<CORSMiddleware, JWTMiddleware> app(CORSMiddleware{}, JWTMiddleware{jwt_secret});

    // =========================================================================
    // Group 0: Connectivity and Database Info Endpoints
    // =========================================================================

    // ----- Endpoint 0.1: Connectivity Test -----
    CROW_ROUTE(app, "/").methods("GET"_method)([]()
                                               {
        crow::json::wvalue res_json;
        res_json["message"] = "C++ backend server is up and running!";
        return crow::response(200, res_json); });

    // ----- Endpoint 0.2: Database Structure -----
    CROW_ROUTE(app, "/api/db-structure").methods("GET"_method)([&db](const crow::request &req)
                                                               {
        try {
            crow::json::wvalue result;
            crow::json::wvalue::list collections_list;
            auto collections = db.list_collection_names();
            for (const auto& name : collections) {
                crow::json::wvalue collection_json;
                collection_json["name"] = name;
                collections_list.push_back(collection_json);
            }
            result["collections"] = std::move(collections_list);
            return crow::response(200, result);
        } catch (const std::exception& e) {
            crow::json::wvalue error;
            error["error"] = e.what();
            return crow::response(500, error);
        } });

    // =========================================================================
    // Group 1: Account Endpoints
    // =========================================================================

    // ----- Endpoint 1.1: Create Account -----
    CROW_ROUTE(app, "/api/create-account").methods("POST"_method)([&accounts_collection](const crow::request &req)
                                                                  {
        auto body = crow::json::load(req.body);
        crow::json::wvalue response_json;
        if (!body) {
            response_json["success"] = false;
            response_json["message"] = "Invalid JSON";
            return crow::response(400, response_json);
        }
        std::string username = body["username"].s();
        std::string password = body["password"].s();
        std::string email = body["email"].s();
        if (username.empty() || password.empty() || email.empty()) {
            response_json["success"] = false;
            response_json["message"] = "username, password, and email are required";
            return crow::response(400, response_json);
        }
        auto filter = bsoncxx::builder::stream::document{}
                        << "username" << username
                        << bsoncxx::builder::stream::finalize;
        auto existing = accounts_collection.find_one(filter.view());
        if (existing) {
            response_json["success"] = false;
            response_json["message"] = "Account already exists";
            return crow::response(409, response_json);
        }
        auto insert_result = accounts_collection.insert_one(
            bsoncxx::builder::stream::document{}
                << "username" << username
                << "password" << password
                << "email" << email
                << bsoncxx::builder::stream::finalize
        );
        if (!insert_result) {
            response_json["success"] = false;
            response_json["message"] = "Failed to create account";
            return crow::response(500, response_json);
        }
        response_json["success"] = true;
        response_json["message"] = "Account created successfully";
        return crow::response(200, response_json); });

    // ----- Endpoint 1.2: Login (with deadlock handling) -----
    // Deadlock Handling: Uses a std::timed_mutex with a timeout to avoid lock acquisition stalling indefinitely.

    CROW_ROUTE(app, "/api/login").methods("POST"_method)([&accounts_collection, &session_mutex, &active_sessions, &jwt_secret](const crow::request &req)
                                                         {
        auto body = crow::json::load(req.body);
        crow::json::wvalue response_json;
        if (!body) {
            response_json["success"] = false;
            response_json["message"] = "Invalid JSON";
            return crow::response(400, response_json);
        }
        std::string username = body["username"].s();
        std::string password = body["password"].s();
        
        std::cout << "Login attempt for username: " << username << std::endl;
        
        if (username.empty() || password.empty()) {
            response_json["success"] = false;
            response_json["message"] = "Username and password are required";
            return crow::response(400, response_json);
        }
        auto filter = bsoncxx::builder::stream::document{}
                        << "username" << username
                        << "password" << password
                        << bsoncxx::builder::stream::finalize;
        auto result = accounts_collection.find_one(filter.view());
        if (!result) {
            std::cout << "Invalid credentials for username: " << username << std::endl;
            response_json["success"] = false;
            response_json["message"] = "Invalid username or password";
            return crow::response(401, response_json);
        }
        auto view = result->view();
        std::string user_id = view["_id"].get_oid().value.to_string();

        // Attempt to acquire the timed_mutex with a timeout (e.g., 1000 ms)
        std::unique_lock<std::timed_mutex> lock(session_mutex, std::chrono::milliseconds(1000));
        if (!lock.owns_lock()) {
            response_json["success"] = false;
            response_json["message"] = "Server busy. Please try again later.";
            return crow::response(503, response_json);
        }
        
        // Critical section: update active_sessions safely
        int current_sessions = active_sessions[username];
        if (current_sessions >= 2) {
            response_json["success"] = false;
            response_json["message"] = "Maximum concurrent sessions reached for this account";
            return crow::response(403, response_json);
        }
        active_sessions[username] = current_sessions + 1;
        // The lock will be automatically released when it goes out of scope.

        std::string token = generate_jwt(user_id, username, jwt_secret);
        
        std::cout << "Login successful for username: " << username << std::endl;
        std::cout << "Generated token of length: " << token.length() << std::endl;
        
        response_json["success"] = true;
        response_json["token"] = token;
        response_json["userId"] = user_id;
        response_json["username"] = username;
        return crow::response(200, response_json); });

    // =========================================================================
    // Group 2: Surf Location Endpoints
    // =========================================================================

    // ----- Endpoint 2.1: Insert Surf Location -----
    CROW_ROUTE(app, "/api/protected/insert-surf-location").methods("POST"_method)([&db](const crow::request &req)
                                                                                  {
    auto body = crow::json::load(req.body);
    if (!body) {
        return crow::response(400, "Invalid JSON");
    }
    
    // Log authenticated user info from request headers
    std::string user_id = req.get_header_value("X-User-ID");
    std::string username = req.get_header_value("X-Username");
    std::cout << "Protected operation by user: " << username << " (ID: " << user_id << ")" << std::endl;
    
    auto collection = db["SurfLocation"];
    
    try {
        // Create a unique index on countryName if it doesn't exist
        // This only needs to be done once - you could move this to your app initialization
        mongocxx::options::index index_options{};
        index_options.unique(true);
        collection.create_index(
            bsoncxx::builder::stream::document{} << "countryName" << 1 << bsoncxx::builder::stream::finalize,
            index_options
        );
        
        // Check explicitly if a location with the same countryName already exists
        std::string country_name = body["countryName"].s();
        bsoncxx::document::value filter = bsoncxx::builder::stream::document{} 
            << "countryName" << country_name
            << bsoncxx::builder::stream::finalize;
            
        auto existing = collection.find_one(filter.view());
        
        if (existing) {
            std::string error_msg = "A surf location with country name '" + country_name + "' already exists";
            std::cout << "Duplicate entry attempt: " << error_msg << std::endl; 
            return crow::response(400, error_msg);
        }
        
        // If no duplicate found, proceed with insertion
        auto insert_result = collection.insert_one(
            bsoncxx::builder::stream::document{}
            << "countryName" << country_name
            << "locationName" << body["locationName"].s()
            << "breakType" << body["breakType"].s()
            << "surfScore" << std::stoi(body["surfScore"].s())
            << "userId" << body["username"].s()
            << "postCount" << 0 // Initialize post count to zero
            << bsoncxx::builder::stream::finalize
        );
        
        if (!insert_result) {
            return crow::response(500, "Insertion failed");
        }
        
        return crow::response(200, "Surf location inserted successfully");
    } catch (const std::exception& e) {
        // Check if the error message contains information about duplicate keys
        std::string error_msg = e.what();
        if (error_msg.find("duplicate key") != std::string::npos) {
            return crow::response(400, "A surf location with this country name already exists (duplicate key error)");
        }
        return crow::response(500, std::string("Error: ") + error_msg);
    } });

    // ----- Endpoint 2.2: Get Surf Locations (Summaries) -----
    // Uses semaphore
    CROW_ROUTE(app, "/api/surf-locations").methods("GET"_method)([&db, &search_semaphore](const crow::request &req)
                                                                 {
        try {
            search_semaphore.acquire();
            std::cout << "Search semaphore acquired\n";

            auto country = req.url_params.get("country");
            auto location = req.url_params.get("location");

            std::cout << "Searching with country=" << (country ? country : "none")
                      << ", location=" << (location ? location : "none") << std::endl;

            bsoncxx::builder::stream::document query_builder;
            if (country && std::string(country).length() > 0) {
                query_builder << "countryName" << bsoncxx::types::b_regex{std::string(country), "i"};
            }
            if (location && std::string(location).length() > 0) {
                query_builder << "locationName" << bsoncxx::types::b_regex{std::string(location), "i"};
            }
            auto query_doc = query_builder << bsoncxx::builder::stream::finalize;
            std::cout << "Final query: " << bsoncxx::to_json(query_doc.view()) << std::endl;

            auto surfLocationColl = db["SurfLocation"];
            std::vector<bsoncxx::document::value> results;
            auto cursor = surfLocationColl.find(query_doc.view());
            for (auto&& doc : cursor) {
                results.push_back(bsoncxx::document::value(doc));
                std::cout << "Found document: " << bsoncxx::to_json(doc) << std::endl;
            }

            std::string json_result;
            if (results.empty()) {
                json_result = "[]";
            } else {
                json_result = "[";
                for (size_t i = 0; i < results.size(); ++i) {
                    json_result += bsoncxx::to_json(results[i]);
                    if (i < results.size() - 1)
                        json_result += ",";
                }
                json_result += "]";
            }

            search_semaphore.release();
            std::cout << "Search semaphore released\n";

            auto res = crow::response(json_result);
            res.code = 200;
            res.add_header("Content-Type", "application/json");
            return res;
        } catch (const std::exception& e) {
            search_semaphore.release();
            std::cout << "Search semaphore released (error)\n";
            std::string error_msg = std::string("{\"error\": \"") + e.what() + "\"}";
            auto res = crow::response(500, error_msg);
            res.add_header("Content-Type", "application/json");
            return res;
        } });

    // ----- Endpoint 2.3: Get Location Details (Location Info & Posts) -----
    // Uses semaphore
    CROW_ROUTE(app, "/api/location-details").methods("GET"_method)([&db, &search_semaphore](const crow::request &req)
                                                                   {
        try {
            search_semaphore.acquire();
            auto locationName = req.url_params.get("locationName");
            if (!locationName) {
                search_semaphore.release();
                return crow::response(400, "{\"error\": \"locationName parameter is required\"}");
            }
            std::string location_str(locationName);

            // Fetch location from "SurfLocation"
            auto surf_collection = db["SurfLocation"];
            bsoncxx::builder::stream::document location_query;
            location_query << "locationName" << location_str;
            auto location_cursor = surf_collection.find(location_query.view());
            std::vector<bsoncxx::document::value> location_results;
            for (auto&& doc : location_cursor) {
                location_results.push_back(bsoncxx::document::value(doc));
            }
            if (location_results.empty()) {
                search_semaphore.release();
                return crow::response(404, "{\"error\": \"Location not found\"}");
            }

            // Fetch posts from "Post"
            auto post_collection = db["Post"];
            bsoncxx::builder::stream::document post_query;
            post_query << "locationName" << location_str;
            auto post_cursor = post_collection.find(post_query.view());
            std::vector<bsoncxx::document::value> post_results;
            for (auto&& doc : post_cursor) {
                post_results.push_back(bsoncxx::document::value(doc));
            }

            // Combine location and posts
            std::vector<bsoncxx::document::value> combined_results;
            combined_results.insert(combined_results.end(), location_results.begin(), location_results.end());
            combined_results.insert(combined_results.end(), post_results.begin(), post_results.end());

            // Build JSON array string
            std::string json_result = "[";
            for (size_t i = 0; i < combined_results.size(); ++i) {
                json_result += bsoncxx::to_json(combined_results[i]);
                if (i < combined_results.size() - 1) {
                    json_result += ",";
                }
            }
            json_result += "]";

            search_semaphore.release();

            auto res = crow::response(json_result);
            res.code = 200;
            res.add_header("Content-Type", "application/json");
            return res;
        } catch (const std::exception& e) {
            search_semaphore.release();
            std::string error_msg = "{\"error\": \"" + std::string(e.what()) + "\"}";
            auto res = crow::response(500, error_msg);
            res.add_header("Content-Type", "application/json");
            return res;
        } });

    // =========================================================================
    // Group 3: Post Endpoints
    // =========================================================================

    // ----- Endpoint 3.1: Create Post -----
    // Explicit Threading/Multithreading Scheduling: Uses std::async(std::launch::async, ...) to schedule the database insertion on a separate thread.
    CROW_ROUTE(app, "/api/protected/create-post").methods("POST"_method)([&db](const crow::request &req)
                                                                         {
        auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "{\"success\": false, \"message\": \"Invalid JSON\"}");
        }
        
        // Get authenticated user info from request headers
        std::string user_id = req.get_header_value("X-User-ID");
        std::string username = req.get_header_value("X-Username");
        std::cout << "Protected create-post by user: " << username << " (ID: " << user_id << ")" << std::endl;
        
        try {
            // 1. Insert the new post
            auto post_collection = db["Post"];
            std::string locationName = body["locationName"].s();
            
            auto insert_result = post_collection.insert_one(
                bsoncxx::builder::stream::document{}
                    << "userId" << body["username"].s()
                    << "locationName" << locationName
                    << "description" << body["description"].s()
                    << "timestamp" << bsoncxx::types::b_date(std::chrono::system_clock::now())
                    << "TotalComments" << 0
                    << bsoncxx::builder::stream::finalize
            );
            
            if (!insert_result) {
                return crow::response(500, "{\"success\": false, \"message\": \"Failed to insert post\"}");
            }
            
            // 2. Increment the postCount in the corresponding SurfLocation document
            auto location_collection = db["SurfLocation"];
            
            auto update_result = location_collection.update_one(
                bsoncxx::builder::stream::document{} 
                    << "locationName" << locationName 
                    << bsoncxx::builder::stream::finalize,
                bsoncxx::builder::stream::document{} 
                    << "$inc" << bsoncxx::builder::stream::open_document
                        << "postCount" << 1
                    << bsoncxx::builder::stream::close_document 
                    << bsoncxx::builder::stream::finalize
            );
            
            if (!update_result || update_result->modified_count() == 0) {
                std::cerr << "Warning: Created post but failed to update location post count for: " 
                          << locationName << std::endl;
                // Still return success as the post was created
            } else {
                std::cout << "Updated postCount for location: " << locationName 
                          << ". Modified count: " << update_result->modified_count() << std::endl;
            }
            
            return crow::response(200, "{\"success\": true, \"message\": \"Post created successfully\"}");
        } catch (const std::exception& e) {
            std::string error_msg = std::string("{\"success\": false, \"message\": \"") + e.what() + "\"}";
            return crow::response(500, error_msg);
        } });

    // =========================================================================
    // Group 4: Comment Endpoints
    // =========================================================================

    // ----- Endpoint 4.1: Create Comment -----
    CROW_ROUTE(app, "/api/protected/create-comment").methods("POST"_method)([&db](const crow::request &req)
                                                                            {
        auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "Invalid JSON");
        }
        
        // Get authenticated user info from request headers
        std::string user_id = req.get_header_value("X-User-ID");
        std::string username = req.get_header_value("X-Username");
        std::cout << "Protected create-comment by user: " << username << " (ID: " << user_id << ")" << std::endl;
        
        try {
            // Begin a multi-operation transaction
            auto comments_collection = db["Comments"];
            auto posts_collection = db["Post"];
            
            // 1. First insert the comment
            auto insert_result = comments_collection.insert_one(
                bsoncxx::builder::stream::document{}
                    << "postId" << body["postId"].s()
                    << "userId" << body["userId"].s()
                    << "commentDescription" << body["description"].s()
                    << "timestamp" << bsoncxx::types::b_date(std::chrono::system_clock::now())
                    << "TotalLikes" << 0
                    << bsoncxx::builder::stream::finalize
            );
            
            if (!insert_result) {
                return crow::response(500, "{\"success\": false, \"message\": \"Failed to create comment\"}");
            }
            
            // 2. Then increment the TotalComments counter in the Post document
            std::string postId = body["postId"].s();
            
            // Build query to identify the post
            bsoncxx::builder::stream::document post_query;
            
            // Check if postId is an ObjectId or a string
            if (postId.length() == 24 && std::all_of(postId.begin(), postId.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            })) {
                // It's likely an ObjectId in string format
                try {
                    bsoncxx::oid oid(postId);
                    post_query << "_id" << oid;
                } catch (const std::exception& e) {
                    // If ObjectId conversion fails, fallback to string match
                    post_query << "_id" << postId;
                }
            } else {
                // Use as string
                post_query << "_id" << postId;
            }
            
            auto update_result = posts_collection.update_one(
                post_query.view(),
                bsoncxx::builder::stream::document{} 
                    << "$inc" << bsoncxx::builder::stream::open_document
                        << "TotalComments" << 1
                    << bsoncxx::builder::stream::close_document 
                    << bsoncxx::builder::stream::finalize
            );
            
            if (!update_result) {
                std::cerr << "Warning: Created comment but failed to update post comment count." << std::endl;
                // We still return success as the comment was created
            } else {
                std::cout << "Updated TotalComments for post " << postId 
                          << ". Modified count: " << update_result->modified_count() << std::endl;
            }
            
            return crow::response(200, "{\"success\": true, \"message\": \"Comment created successfully\"}");
        } catch (const std::exception& e) {
            std::cerr << "Error creating comment: " << e.what() << std::endl;
            return crow::response(500, std::string("{\"success\": false, \"message\": \"") + e.what() + "\"}");
        } });

    // ----- Endpoint 4.2: Get Post Comments -----
    // Explicit Multi/Thread Scheduling: Also uses std::async to retrieve and process comments in a separate thread.
    CROW_ROUTE(app, "/api/post-comments").methods("GET"_method)([&db](const crow::request &req)
                                                                {
        auto postId = req.url_params.get("postId");
        if (!postId) {
            return crow::response(400, "Missing postId parameter");
        }
        
        // Use std::async to schedule the retrieval and processing of comments.
        auto futureResult = std::async(std::launch::async, [&db, postId]() -> crow::response {
            auto comments_collection = db["Comments"];
            bsoncxx::builder::stream::document query_builder;
            query_builder << "postId" << postId;
            auto cursor = comments_collection.find(query_builder.view());
            
            crow::json::wvalue result;
            crow::json::wvalue::list comments_list;
            for (auto&& doc : cursor) {
                std::string doc_str = bsoncxx::to_json(doc);
                auto rdoc = crow::json::load(doc_str);
                if (!rdoc)
                    continue;
                crow::json::wvalue doc_json = std::move(rdoc);
                if (doc_json["_id"].t() == crow::json::type::Object) {
                    std::string oid_dump = doc_json["_id"]["$oid"].dump();
                    if (!oid_dump.empty() && oid_dump.front() == '"' && oid_dump.back() == '"') {
                        oid_dump = oid_dump.substr(1, oid_dump.size() - 2);
                    }
                    doc_json["commentId"] = std::move(crow::json::wvalue(oid_dump));
                }
                comments_list.push_back(doc_json);
            }
            result["comments"] = std::move(comments_list);
            crow::response res(result);
            res.code = 200;
            res.add_header("Content-Type", "application/json");
            return res;
        });
        
        return futureResult.get(); });

    // ----- Endpoint 4.3: Like Comment -----
    CROW_ROUTE(app, "/api/protected/like-comment").methods("POST"_method)([&db](const crow::request &req)
                                                                          {
        auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "Invalid JSON");
        }
        
        // Get authenticated user info from request headers
        std::string user_id = req.get_header_value("X-User-ID");
        std::string username = req.get_header_value("X-Username");
        std::cout << "Protected like-comment by user: " << username << " (ID: " << user_id << ")" << std::endl;
        
        try {
            auto comments_collection = db["Comments"];
            std::string commentIdStr = body["commentId"].s();
            bsoncxx::oid commentId(commentIdStr);
            auto update_result = comments_collection.update_one(
                bsoncxx::builder::stream::document{} 
                    << "_id" << commentId 
                    << bsoncxx::builder::stream::finalize,
                bsoncxx::builder::stream::document{} 
                    << "$inc" << bsoncxx::builder::stream::open_document
                    << "TotalLikes" << 1
                    << bsoncxx::builder::stream::close_document 
                    << bsoncxx::builder::stream::finalize
            );
            if (!update_result) {
                return crow::response(500, "{\"success\": false, \"message\": \"Failed to update comment like count\"}");
            }
            return crow::response(200, "{\"success\": true, \"message\": \"Comment liked successfully\"}");
        } catch (const std::exception& e) {
            return crow::response(500, std::string("Error: ") + e.what());
        } });

    // ADD IN LOGOUT THINGY HERE
    CROW_ROUTE(app, "/api/logout").methods("POST"_method)([&session_mutex, &active_sessions](const crow::request &req)
                                                          {
            auto body = crow::json::load(req.body);
            if (!body) {
                return crow::response(400, "Invalid JSON");
            }
            
            std::string username = body["username"].s();
            
            // Acquire mutex with timeout
            std::unique_lock<std::timed_mutex> lock(session_mutex, std::chrono::milliseconds(1000));
            if (!lock.owns_lock()) {
                return crow::response(503, "Server busy. Please try again later.");
            }
            
            // Decrement session count only if it's greater than 0
            if (active_sessions.find(username) != active_sessions.end() && active_sessions[username] > 0) {
                active_sessions[username]--;
                std::cout << "User logged out: " << username << ". Remaining sessions: " << active_sessions[username] << std::endl;
            }
            
            return crow::response(200, "Logout successful"); });

    // =========================================================================
    // Start the Server
    // =========================================================================
    std::cout << "Starting server on port " << port << "...\n";
    app.port(port).multithreaded().run();
    return 0;
}