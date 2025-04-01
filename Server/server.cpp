#include "crow_all.h"
#include <bsoncxx/json.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>
#include <jwt-cpp/jwt.h>
#include <chrono>
#include <cstdlib>
#include <string>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <semaphore>   // for std::counting_semaphore
#include <vector>
#include <fstream>

/*
Legend:

Group 0: Connectivity and Database Info Endpoints
  0.1 - Connectivity Test
      GET "/" 
      Returns a simple message confirming the backend server is running.
  0.2 - Database Structure
      GET "/api/db-structure" 
      Returns the list of collection names in the database.

Group 1: Account Endpoints
  1.1 - Create Account
      POST "/api/create-account"
      Validates input, checks for existing account, and inserts a new account.
  1.2 - Login
      POST "/api/login"
      Validates credentials and returns user details with a JWT token.
      Deadlock Handling: Uses a std::timed_mutex with a timeout to avoid lock acquisition stalling indefinitely.


Group 2: Surf Location Endpoints
  2.1 - Insert Surf Location
      POST "/api/insert-surf-location"
      Inserts a new surf location document into the database.
  2.2 - Surf Locations (Summaries)
      GET "/api/surf-locations"
      Retrieves surf location summaries with optional country and location filters.
      Uses semahpore

  2.3 - Location Details (Granular & Posts)
      GET "/api/location-details"
      Retrieves detailed information for a location along with its associated posts.
      Uses semahpore

Group 3: Post Endpoints
  3.1 - Create Post
      POST "/api/create-post"
      Inserts a new post document with initial like and comment counts set to zero.
      Explicit Thread Scheduling: Uses std::async(std::launch::async, ...) to schedule the database insertion on a separate thread.

Group 4: Comment Endpoints
  4.1 - Create Comment
      POST "/api/create-comment"
      Inserts a new comment for a post.
  4.2 - Get Post Comments
      GET "/api/post-comments"
      Retrieves all comments (with like counts) for a given post.
      Explicit Thread Scheduling: Also uses std::async to retrieve and process comments in a separate thread.



  4.3 - Like Comment
      POST "/api/like-comment"
      Increments the like count for a comment.
*/


// -----------------------------------------------------------------------------
// loadDotEnv: Loads environment variables from a .env file into the process
// -----------------------------------------------------------------------------
void loadDotEnv(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open .env file at " << path << std::endl;
        return;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos)
            continue;
        size_t end = line.find_last_not_of(" \t");
        line = line.substr(start, end - start + 1);
        if (line.empty() || line[0] == '#')
            continue;
        size_t delim_pos = line.find('=');
        if (delim_pos == std::string::npos)
            continue;
        std::string key = line.substr(0, delim_pos);
        std::string value = line.substr(delim_pos + 1);
        if (!value.empty() && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        setenv(key.c_str(), value.c_str(), 1);
    }
    file.close();
}

// -----------------------------------------------------------------------------
// Load JWT secret from environment variable (after .env is loaded)
// -----------------------------------------------------------------------------
const char* jwt_secret_env = std::getenv("JWT_SECRET");
const std::string jwt_secret = jwt_secret_env ? jwt_secret_env : "fallback_jwt_secret";

// -----------------------------------------------------------------------------
// CORSMiddleware: Attaches CORS headers & handles OPTIONS requests
// -----------------------------------------------------------------------------
struct CORSMiddleware {
    struct context {};

    void before_handle(crow::request& req, crow::response& res, context& ctx) {
        // If the request is a preflight OPTIONS request, respond with 200 OK.
        if (req.method == crow::HTTPMethod::OPTIONS) {
            res.code = 200;
            res.add_header("Access-Control-Allow-Origin", "*");
            res.add_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            // Include "Origin" if any fetch call sets it manually.
            res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization, Accept, Origin");
            res.end();
            return;
        }
    }

    void after_handle(crow::request& req, crow::response& res, context& ctx) {
        // Ensure that all responses have the CORS headers.
        res.add_header("Access-Control-Allow-Origin", "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization, Accept, Origin");
    }
};

// -----------------------------------------------------------------------------
// JWT Utility Functions
// -----------------------------------------------------------------------------
std::string generate_jwt(const std::string& user_id, const std::string& username) {
    auto token = jwt::create()
        .set_issuer("EddieAikau")
        .set_type("JWS")
        .set_subject(user_id)
        .set_audience("EddieAikauApp")
        .set_payload_claim("username", jwt::claim(username))
        .set_expires_at(std::chrono::system_clock::now() + std::chrono::hours(24))
        .sign(jwt::algorithm::hs256{jwt_secret});
    return token;
}

bool verify_jwt(const std::string& token, std::string& user_id, std::string& username) {
    try {
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
                        .allow_algorithm(jwt::algorithm::hs256{jwt_secret})
                        .with_issuer("EddieAikau");
        verifier.verify(decoded);
        user_id = decoded.get_subject();
        username = decoded.get_payload_claim("username").as_string();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "JWT verification error: " << e.what() << std::endl;
        return false;
    }
}

// -----------------------------------------------------------------------------
// JWTMiddleware: Protects routes with "/api/protected"
// -----------------------------------------------------------------------------
struct JWTMiddleware {
    struct context {};
    void before_handle(crow::request& req, crow::response& res, context& ctx) {
        if (req.url.find("/api/protected") != std::string::npos) {
            auto auth_header = req.get_header_value("Authorization");
            if (auth_header.empty() || auth_header.find("Bearer ") != 0) {
                res.code = 401;
                res.write("Unauthorized: Missing or invalid token");
                res.end();
                return;
            }
            std::string token = auth_header.substr(7);
            std::string user_id, username;
            if (!verify_jwt(token, user_id, username)) {
                res.code = 401;
                res.write("Unauthorized: Token verification failed");
                res.end();
                return;
            }
        }
    }
    void after_handle(crow::request& req, crow::response& res, context& ctx) { }
};

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main() {
    loadDotEnv(".env");

    const char* mongo_uri_env = std::getenv("MONGO_URI");
    std::string mongo_uri = mongo_uri_env ? mongo_uri_env : "mongodb://localhost:27017";

    const char* db_name_env = std::getenv("DATABASE");
    std::string db_name = db_name_env ? db_name_env : "defaultDatabase";

    const char* port_env = std::getenv("PORT");
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

    crow::App<CORSMiddleware, JWTMiddleware> app;

    // =========================================================================
    // Group 0: Connectivity and Database Info Endpoints
    // =========================================================================

    // ----- Endpoint 0.1: Connectivity Test -----
    CROW_ROUTE(app, "/").methods("GET"_method)
    ([]() {
        crow::json::wvalue res_json;
        res_json["message"] = "C++ backend server is up and running!";
        return crow::response(200, res_json);
    });

    // ----- Endpoint 0.2: Database Structure -----
    CROW_ROUTE(app, "/api/db-structure").methods("GET"_method)
    ([&db](const crow::request& req) {
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
        }
    });

    // =========================================================================
    // Group 1: Account Endpoints
    // =========================================================================

    // ----- Endpoint 1.1: Create Account -----
    CROW_ROUTE(app, "/api/create-account").methods("POST"_method)
    ([&accounts_collection](const crow::request& req) {
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
        return crow::response(200, response_json);
    });

    // ----- Endpoint 1.2: Login (with deadlock handling) -----
    //Deadlock Handling: Uses a std::timed_mutex with a timeout to avoid lock acquisition stalling indefinitely.

    CROW_ROUTE(app, "/api/login").methods("POST"_method)
    ([&accounts_collection, &session_mutex, &active_sessions](const crow::request& req) {
        auto body = crow::json::load(req.body);
        crow::json::wvalue response_json;
        if (!body) {
            response_json["success"] = false;
            response_json["message"] = "Invalid JSON";
            return crow::response(400, response_json);
        }
        std::string username = body["username"].s();
        std::string password = body["password"].s();
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

        std::string token = generate_jwt(user_id, username);
        response_json["success"] = true;
        response_json["token"] = token;
        response_json["userId"] = user_id;
        response_json["username"] = username;
        return crow::response(200, response_json);
    });



    // =========================================================================
    // Group 2: Surf Location Endpoints
    // =========================================================================

    // ----- Endpoint 2.1: Insert Surf Location -----
    CROW_ROUTE(app, "/api/insert-surf-location").methods("POST"_method)
    ([&db](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "Invalid JSON");
        }
        auto collection = db["SurfLocation"];
        try {
            auto insert_result = collection.insert_one(
                bsoncxx::builder::stream::document{}
                    << "countryName" << body["countryName"].s()
                    << "locationName" << body["locationName"].s()
                    << "breakType" << body["breakType"].s()
                    << "surfScore" << std::stoi(body["surfScore"].s())
                    << "userId" << body["userId"].s()
                    << bsoncxx::builder::stream::finalize
            );
            if (!insert_result) {
                return crow::response(500, "Insertion failed");
            }
            return crow::response(200, "Surf location inserted successfully");
        } catch (const std::exception& e) {
            return crow::response(500, std::string("Error: ") + e.what());
        }
    });

    // ----- Endpoint 2.2: Get Surf Locations (Summaries) -----
    //Uses semaphore
    CROW_ROUTE(app, "/api/surf-locations").methods("GET"_method)
    ([&db, &search_semaphore](const crow::request& req) {
        try {
            search_semaphoreacquire();
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
        }
    });

    // ----- Endpoint 2.3: Get Location Details (Location Info & Posts) -----
    //Uses semaphore
    CROW_ROUTE(app, "/api/location-details").methods("GET"_method)
    ([&db, &search_semaphore](const crow::request& req) {
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
        }
    });

    // =========================================================================
    // Group 3: Post Endpoints
    // =========================================================================

    // ----- Endpoint 3.1: Create Post -----
    //Explicit Threading/Multithreading Scheduling: Uses std::async(std::launch::async, ...) to schedule the database insertion on a separate thread.
    CROW_ROUTE(app, "/api/create-post").methods("POST"_method)
    ([&db](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "{\"success\": false, \"message\": \"Invalid JSON\"}");
        }
        
        // Use std::async to explicitly schedule the database insertion in another thread.
        auto futureResult = std::async(std::launch::async, [&db, body]() -> std::string {
            try {
                auto post_collection = db["Post"];
                auto insert_result = post_collection.insert_one(
                    bsoncxx::builder::stream::document{}
                        << "userId" << body["userId"].s()
                        << "locationName" << body["locationName"].s()
                        << "description" << body["description"].s()
                        << "timestamp" << bsoncxx::types::b_date(std::chrono::system_clock::now())
                        << "TotalLikes" << 0
                        << "TotalComments" << 0
                        << bsoncxx::builder::stream::finalize
                );
                if (!insert_result) {
                    return std::string("{\"success\": false, \"message\": \"Failed to insert post\"}");
                }
                return std::string("{\"success\": true, \"message\": \"Post created successfully\"}");
            } catch (const std::exception& e) {
                return std::string("{\"success\": false, \"message\": \"") + e.what() + "\"}";
            }
        });
        
        // Wait for the task to complete and return its result.
        std::string result_str = futureResult.get();
        return crow::response(200, result_str);
    });


    // =========================================================================
    // Group 4: Comment Endpoints
    // =========================================================================

    // ----- Endpoint 4.1: Create Comment -----
    CROW_ROUTE(app, "/api/create-comment").methods("POST"_method)
    ([&db](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "Invalid JSON");
        }
        try {
            auto comments_collection = db["Comments"];
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
            return crow::response(200, "{\"success\": true, \"message\": \"Comment created successfully\"}");
        } catch (const std::exception& e) {
            return crow::response(500, std::string("Error: ") + e.what());
        }
    });

    // ----- Endpoint 4.2: Get Post Comments -----
    //Explicit Multi/Thread Scheduling: Also uses std::async to retrieve and process comments in a separate thread.
    CROW_ROUTE(app, "/api/post-comments").methods("GET"_method)
    ([&db](const crow::request& req) {
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
        
        return futureResult.get();
    });


    // ----- Endpoint 4.3: Like Comment -----
    CROW_ROUTE(app, "/api/like-comment").methods("POST"_method)
    ([&db](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "Invalid JSON");
        }
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
        }
    });

    // =========================================================================
    // Start the Server
    // =========================================================================
    std::cout << "Starting server on port " << port << "...\n";
    app.port(port).multithreaded().run();
    return 0;
}
