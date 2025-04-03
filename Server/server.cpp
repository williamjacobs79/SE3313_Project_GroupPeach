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
  0.1 - Connectivity Test [WILL]
      GET "/"
      Returns a simple message confirming the backend server is running.

  0.2 - Database Structure [WILL]
      GET "/api/db-structure"
      Returns the list of collection names in the database.

Group 1: Account Endpoints
  1.1 - Create Account [FELIX]
      POST "/api/create-account"
      Validates input, checks for existing account, and inserts a new account.
      Concurrency Handling: uses a mutex to lock critical section

  1.2 - Login [FELIX]
      POST "/api/login"
      Validates credentials and returns user details with a JWT token.
      Deadlock Handling: Uses a std::timed_mutex with a timeout to avoid lock acquisition stalling indefinitely.

  1.3 - Logout [FELIX]
      POST "/api/logout"
      Removes a user from active_sessions
      Deadlock Handling: Uses a std::timed_mutex with a timeout to avoid lock acquisition stalling indefinitely.

Group 2: Surf Location Endpoints
  2.1 - Insert Surf Location [CHRISTIAN]
      POST "/api/protected/insert-surf-location"
      Inserts a new surf location document into the database.
      Concurrency Handling: uses a std:mutex and lock with surf_location_mutex

  2.2 - Surf Locations (Summaries) [MARK]
      GET "/api/surf-locations"
      Retrieves surf location summaries with optional country and location filters.
      Overload Prevention: Uses a semaphore to limit concurrent searches

  2.3 - Location Details (Granular & Posts) [MARK]
      GET "/api/location-details"
      Retrieves detailed information for a location along with its associated posts.
      Overload Prevention: Uses a semaphore to limit concurrent searches

Group 3: Post Endpoints
  3.1 - Create Post [CHRISTIAN]
      POST "/api/protected/create-post"
      Inserts a new post document with initial like and comment counts set to zero.
      Explicit Thread Scheduling: Uses std::async(std::launch::async, ...) to schedule the database insertion on a separate thread.

Group 4: Comment Endpoints
  4.1 - Create Comment [BILLY]
      POST "/api/protected/create-comment"
      Inserts a new comment for a post.
      Concurrency Handling: uses a mutex to lock critical section

  4.2 - Get Post Comments [BILLY]
      GET "/api/post-comments"
      Retrieves all comments (with like counts) for a given post.
      Explicit Thread Scheduling: Also uses std::async to retrieve and process comments in a separate thread.

  4.3 - Like Comment [BILLY] XX
      POST "/api/protected/like-comment"
      Increments the like count for a comment.
      Overload Prevention: Uses a semaphore to limit concurrent searches

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

// attaches CORS headers & handles OPTIONS requests
struct CORSMiddleware
{
    // structure empty - don't need to store any of this
    struct context
    {
    };

    void before_handle(crow::request &req, crow::response &res, context &ctx)
    {
        // if the request is a preflight OPTIONS request, respond with 200 OK.
        if (req.method == crow::HTTPMethod::OPTIONS)
        {
            res.code = 200;
            res.add_header("Access-Control-Allow-Origin", "*");
            res.add_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization, Accept, Origin");
            res.end();
            return;
        }
    }

    void after_handle(crow::request &req, crow::response &res, context &ctx)
    {
        // ensure that all responses have the CORS headers.
        res.add_header("Access-Control-Allow-Origin", "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization, Accept, Origin");
    }
};

// generates JWT token
std::string generate_jwt(const std::string &user_id, const std::string &username, const std::string &jwt_secret)
{
    // creates a signed JWT with username and 24hr expiry
    auto token = jwt::create()
                     .set_issuer("EddieAikau")
                     .set_type("JWS")
                     .set_subject(user_id)
                     .set_audience("EddieAikauApp")
                     .set_payload_claim("username", jwt::claim(username))
                     .set_expires_at(std::chrono::system_clock::now() + std::chrono::hours(24))
                     .sign(jwt::algorithm::hs256{jwt_secret});

    // log JWT generation
    std::cout << "Generated JWT token for user: " << username << " with ID: " << user_id << std::endl;

    // return tokwn
    return token;
}

// verifies received JWT token
bool verify_jwt(const std::string &token, std::string &user_id, std::string &username, const std::string &jwt_secret)
{
    try
    {
        // log verifying token
        std::cout << "Verifying JWT token..." << std::endl;

        // decode token
        auto decoded = jwt::decode(token);

        // log debug info
        std::cout << "Token issuer: " << decoded.get_issuer() << std::endl;
        std::cout << "Token subject: " << decoded.get_subject() << std::endl;

        // verify issuer and secret
        auto verifier = jwt::verify()
                            .allow_algorithm(jwt::algorithm::hs256{jwt_secret})
                            .with_issuer("EddieAikau");
        verifier.verify(decoded);

        // ger userID and name
        user_id = decoded.get_subject();
        username = decoded.get_payload_claim("username").as_string();

        // log user info
        std::cout << "JWT verification successful. User ID: " << user_id << ", Username: " << username << std::endl;

        // return true (b/c auth was ok)
        return true;
    }
    catch (const std::exception &e)
    {
        // log error and return false
        std::cerr << "JWT verification error: " << e.what() << std::endl;
        return false;
    }
}

// middleware to check received JWT for routes with "/api/protected"
struct JWTMiddleware
{
    // constructor initializes middleware with key
    JWTMiddleware(const std::string &secret) : jwt_secret(secret) {}

    // secret key
    std::string jwt_secret;

    // context structure to keep id and name
    struct context
    {
        std::string user_id;
        std::string username;
    };

    // method called before handling request
    void before_handle(crow::request &req, crow::response &res, context &ctx)
    {
        // check if protected route
        if (req.url.find("/api/protected") != std::string::npos)
        {
            // log protected route being accessed
            std::cout << "Protected route accessed: " << req.url << std::endl;

            // get auth header and check it starts with "Bearer"
            auto auth_header = req.get_header_value("Authorization");
            if (auth_header.empty() || auth_header.find("Bearer ") != 0)
            {
                std::cout << "Authorization header missing or invalid: " << auth_header << std::endl;
                res.code = 401;
                res.write("Unauthorized: Missing or invalid token");
                res.end();
                return;
            }

            // remove "Bearer" and log 10 chars from token (for security)
            std::string token = auth_header.substr(7);
            std::cout << "Token extracted from header: " << (token.length() > 10 ? token.substr(0, 10) + "..." : token) << std::endl;

            // verify and extract user info
            if (!verify_jwt(token, ctx.user_id, ctx.username, jwt_secret))
            {
                std::cout << "JWT verification failed" << std::endl;
                res.code = 401;
                res.write("Unauthorized: Token verification failed");
                res.end();
                return;
            }

            // add user info to request for handlers to use
            req.add_header("X-User-ID", ctx.user_id);
            req.add_header("X-Username", ctx.username);
            std::cout << "JWT verification successful, added user headers" << std::endl;
        }
    }

    // method called after handling request (empty)
    void after_handle(crow::request &req, crow::response &res, context &ctx) {}
};

// main function (where application runs from)
int main()
{
    // load in environment variables
    loadDotEnv(".env");

    // get JWT secret from environment (or use fallback)
    const char *jwt_secret_env = std::getenv("JWT_SECRET");
    std::string jwt_secret = jwt_secret_env ? jwt_secret_env : "fallback_jwt_secret";

    // retreive env MongoDB connection (or use local default)
    const char *mongo_uri_env = std::getenv("MONGO_URI");
    std::string mongo_uri = mongo_uri_env ? mongo_uri_env : "mongodb://localhost:27017";

    // get env database name (or use defaultDatabase)
    const char *db_name_env = std::getenv("DATABASE");
    std::string db_name = db_name_env ? db_name_env : "defaultDatabase";

    // get env port (or use 3000)
    const char *port_env = std::getenv("PORT");
    int port = port_env ? std::stoi(port_env) : 3000;

    // log config setting
    std::cout << "Using Mongo URI: " << mongo_uri << "\n";
    std::cout << "Using Database: " << db_name << "\n";
    std::cout << "Using Port: " << port << "\n";
    std::cout << "JWT Secret: " << (jwt_secret_env ? "Loaded from ENV" : "Fallback used!") << "\n";

    mongocxx::instance instance{};                        // initialize MongDB C++ driver
    mongocxx::client client{mongocxx::uri{mongo_uri}};    // create connection
    auto db = client[db_name];                            // reference to database
    auto accounts_collection = db["Accounts"];            // reference to Accounts collection
    std::counting_semaphore<3> search_semaphore(3);       // semaphore to limit concurrent searches to 3
    std::timed_mutex session_mutex;                       // mutex for sessions
    std::mutex surf_location_mutex;                       // for thread-safe operations when adding new location
    std::mutex account_creation_mutex;                    // for thread-safe operations when creating new accounts
    std::mutex comment_creation_mutex;                    // for thread-safe operations when creating comments
    std::counting_semaphore<5> like_semaphore(5);         // semaphore to limit concurrent like operations to 5
    std::unordered_map<std::string, int> active_sessions; // map of all active sessions (for the limit we impose)

    // initialize Crow framework with CORS and JWT middleware
    crow::App<CORSMiddleware, JWTMiddleware> app(CORSMiddleware{}, JWTMiddleware{jwt_secret});

    // =========================================================================
    // Group 0: Connectivity and Database Info Endpoints
    // =========================================================================

    // ----- Endpoint 0.1: Connectivity Test -----
    // for root endpoint, send a JSON response back
    CROW_ROUTE(app, "/").methods("GET"_method)([]()
                                               {
        crow::json::wvalue res_json; //make JSON
        res_json["message"] = "C++ backend server is up and running!"; //add to JSON
        return crow::response(200, res_json); }); // respond back

    // ----- Endpoint 0.2: Database Structure -----
    // to show database structure
    CROW_ROUTE(app, "/api/db-structure").methods("GET"_method)([&db](const crow::request &req)
                                                               {
        try {
            crow::json::wvalue result;                      //JSON to hold results
            crow::json::wvalue::list collections_list;      //JSON to hold collection info
            auto collections = db.list_collection_names();  //get all collections from db
            for (const auto& name : collections) {          //loop through collection names
                crow::json::wvalue collection_json;         //create JSON
                collection_json["name"] = name;             //add collection name
                collections_list.push_back(collection_json);//add collection to list
            }
            result["collections"] = std::move(collections_list);  //add collection to outer JSON
            return crow::response(200, result);                   //rsend JSON of collection names
        } catch (const std::exception& e) {
            //if error, create error JSON and return that
            crow::json::wvalue error;
            error["error"] = e.what();
            return crow::response(500, error);
        } });

    // =========================================================================
    // Group 1: Account Endpoints
    // =========================================================================

    // ----- Endpoint 1.1: Create Account -----
    // Concurrency Handling: uses a mutex to lock critical section
    CROW_ROUTE(app, "/api/create-account").methods("POST"_method)([&accounts_collection, &account_creation_mutex](const crow::request &req)
                                                                  {
    auto body = crow::json::load(req.body); //parse JSON from request body
    crow::json::wvalue response_json;       //initialize JSON response
    
    //if JSON invalid, return error
    if (!body) {
        response_json["success"] = false;
        response_json["message"] = "Invalid JSON";
        return crow::response(400, response_json);
    }

    //extract user information
    std::string username = body["username"].s();
    std::string password = body["password"].s();
    std::string email = body["email"].s();
    
    //if any field empty, return error response
    if (username.empty() || password.empty() || email.empty()) {
        response_json["success"] = false;
        response_json["message"] = "username, password, and email are required";
        return crow::response(400, response_json);
    }

    // lock the mutex to ensure thread safety during account creation
    std::lock_guard<std::mutex> lock(account_creation_mutex);
    
    // CRITICAL SECTION starts
    
    //create filter to check if username exists
    auto filter = bsoncxx::builder::stream::document{}
                    << "username" << username
                    << bsoncxx::builder::stream::finalize;
    
    //check if one exists
    auto existing = accounts_collection.find_one(filter.view());

    //if one does exist, return error response
    if (existing) {
        response_json["success"] = false;
        response_json["message"] = "Account already exists";
        return crow::response(409, response_json);
    }
    
    //construct document for new user and insert
    auto insert_result = accounts_collection.insert_one(
        bsoncxx::builder::stream::document{}
            << "username" << username
            << "password" << password
            << "email" << email
            << bsoncxx::builder::stream::finalize
    );
    
    // CRITICAL SECTION ends (lock is automatically released)

    //if insert failed, return error response
    if (!insert_result) {
        response_json["success"] = false;
        response_json["message"] = "Failed to create account";
        return crow::response(500, response_json);
    }

    //insert succeed, return success response
    response_json["success"] = true;
    response_json["message"] = "Account created successfully";
    return crow::response(200, response_json); });

    // ----- Endpoint 1.2: Login (with deadlock handling) -----
    // Deadlock Handling: Uses a std::timed_mutex with a timeout to avoid lock acquisition stalling indefinitely.
    CROW_ROUTE(app, "/api/login").methods("POST"_method)([&accounts_collection, &session_mutex, &active_sessions, &jwt_secret](const crow::request &req)
                                                         {
        auto body = crow::json::load(req.body); //parse JSON from request body
        crow::json::wvalue response_json;       //initialize JSON response
        
        //if JSON invalid, return error response
        if (!body) {
            response_json["success"] = false;
            response_json["message"] = "Invalid JSON";
            return crow::response(400, response_json);
        }

        //extract user info
        std::string username = body["username"].s();
        std::string password = body["password"].s();
        
        //log attempt
        std::cout << "Login attempt for username: " << username << std::endl;
        
        //if field empty, return error response
        if (username.empty() || password.empty()) {
            response_json["success"] = false;
            response_json["message"] = "Username and password are required";
            return crow::response(400, response_json);
        }

        //create filter to find matching username and password
        auto filter = bsoncxx::builder::stream::document{}
                        << "username" << username
                        << "password" << password
                        << bsoncxx::builder::stream::finalize;

        //search database
        auto result = accounts_collection.find_one(filter.view());
        
        //if nothing foud, return error response
        if (!result) {
            std::cout << "Invalid credentials for username: " << username << std::endl;
            response_json["success"] = false;
            response_json["message"] = "Invalid username or password";
            return crow::response(401, response_json);
        }

        //extract objectID for user (mongo id)
        auto view = result->view();
        std::string user_id = view["_id"].get_oid().value.to_string();

        // acquire the timed_mutex with a timeout (1s)
        std::unique_lock<std::timed_mutex> lock(session_mutex, std::chrono::milliseconds(1000));
        
        //if couldn't be acquired, server is too busy
        if (!lock.owns_lock()) {
            response_json["success"] = false;
            response_json["message"] = "Server busy. Please try again later.";
            return crow::response(503, response_json);
        }
        
        // CRITICAL SECTION (only after lock), update active_Sessions safely
        int current_sessions = active_sessions[username]; //get current sessions for username
        if (current_sessions >= 2) { //if more than two, return error response
            response_json["success"] = false;
            response_json["message"] = "Maximum concurrent sessions reached for this account";
            return crow::response(403, response_json);
        }
        
        //increase number of current sessions for user
        active_sessions[username] = current_sessions + 1;
        
        // lock auto-releases here (out of scope)

        //generate JWT
        std::string token = generate_jwt(user_id, username, jwt_secret);
        
        //log success
        std::cout << "Login successful for username: " << username << std::endl;
        std::cout << "Generated token of length: " << token.length() << std::endl;
        
        //add values to response json and send
        response_json["success"] = true;
        response_json["token"] = token;
        response_json["userId"] = user_id;
        response_json["username"] = username;
        return crow::response(200, response_json); });

    // ----- Endpoint 1.3: Logout (with deadlock handling) -----
    // Deadlock Handling: Uses a std::timed_mutex with a timeout to avoid lock acquisition stalling indefinitely.
    CROW_ROUTE(app, "/api/logout").methods("POST"_method)([&session_mutex, &active_sessions](const crow::request &req)
                                                          {
            
            //parse JSON from request body
            auto body = crow::json::load(req.body);
            
            //if invalid JSON, return error
            if (!body) {
                return crow::response(400, "Invalid JSON");
            }
            
            //get out username
            std::string username = body["username"].s();
            
            // acquire the timed_mutex with a timeout (1s)
            std::unique_lock<std::timed_mutex> lock(session_mutex, std::chrono::milliseconds(1000));
            
            //if couldn't be acquired, server is too busy
            if (!lock.owns_lock()) {
                return crow::response(503, "Server busy. Please try again later.");
            }
            
            // CRITICAL SECTION (only after lock), update active_Sessions safely
            if (active_sessions.find(username) != active_sessions.end() && active_sessions[username] > 0) {
                active_sessions[username]--; //decrement if greater than 0 and log
                std::cout << "User logged out: " << username << ". Remaining sessions: " << active_sessions[username] << std::endl;
            }

            /// lock auto-releases here (out of scope)
            
            //return successful logout
            return crow::response(200, "Logout successful"); });

    // =========================================================================
    // Group 2: Surf Location Endpoints
    // =========================================================================

    // ----- Endpoint 2.1: Insert Surf Location -----
    // Concurrency Handling: uses a std:mutex and lock with surf_location_mutex
    CROW_ROUTE(app, "/api/protected/insert-surf-location").methods("POST"_method)([&db, &surf_location_mutex](const crow::request &req)
                                                                                  {
        // lock to prevent concurrency issues (only want 1 at a time in here)
        std::lock_guard<std::mutex> lock(surf_location_mutex);
        
        //parse JSON body from request
        auto body = crow::json::load(req.body);
        
        //if invalid JSON, return error response
        if (!body) {
            return crow::response(400, "Invalid JSON");
        }
        
        //extract user information and log
        std::string user_id = req.get_header_value("X-User-ID");
        std::string username = req.get_header_value("X-Username");
        std::cout << "Protected operation by user: " << username << " (ID: " << user_id << ")" << std::endl;
        
        //reference to collection in db
        auto collection = db["SurfLocation"];
        
        try {
            // get location name
            std::string location_name = body["locationName"].s();
            
            // conunt matching document
            auto count = collection.count_documents(
                bsoncxx::builder::stream::document{} 
                << "locationName" << location_name
                << bsoncxx::builder::stream::finalize
            );
            
            // if already exists
            if (count > 0) {
                return crow::response(400, "A surf location with this location name already exists");
            }
            
            // no duplicates, continue with insert (and insert all values)
            auto insert_result = collection.insert_one(
                bsoncxx::builder::stream::document{}
                << "countryName" << body["countryName"].s()
                << "locationName" << location_name
                << "breakType" << body["breakType"].s()
                << "surfScore" << std::stoi(body["surfScore"].s())
                << "userId" << body["username"].s()
                << "postCount" << 0 // initialize post count (0)
                << bsoncxx::builder::stream::finalize
            );
            
            //if insert failed, return error
            if (!insert_result) {
                return crow::response(500, "Insertion failed");
            }
            
            //return successful insert
            return crow::response(200, "Surf location inserted successfully");
        }
        catch (const std::exception& e) {
            //catches errors (duplicate location)
            
            //error string
            std::string error_msg = e.what();
            
            //return duplicate key errors (mongoDb)
            if (error_msg.find("duplicate key") != std::string::npos || 
                error_msg.find("E11000") != std::string::npos) {
                return crow::response(400, "A surf location with this location name already exists");
            }
            //return other errors
            return crow::response(500, std::string("Error: ") + error_msg);
        } });

    // ----- Endpoint 2.2: Get Surf Locations (Summaries) -----
    // Overload Prevention: Uses a semaphore to limit concurrent searches
    CROW_ROUTE(app, "/api/surf-locations").methods("GET"_method)([&db, &search_semaphore](const crow::request &req)
                                                                 {
        try {
            //acquire semaphore
            search_semaphore.acquire();
            
            //log semaphore acquires
            std::cout << "Search semaphore acquired\n";

            //extract URL parameters
            auto country = req.url_params.get("country");
            auto location = req.url_params.get("location");

            //log parameters
            std::cout << "Searching with country=" << (country ? country : "none")
                      << ", location=" << (location ? location : "none") << std::endl;

            //build MongoDB query
            bsoncxx::builder::stream::document query_builder;
            
            //add country filter (i means case in-sensitive)
            if (country && std::string(country).length() > 0) {
                query_builder << "countryName" << bsoncxx::types::b_regex{std::string(country), "i"};
            }

            //add location filter  (i means case in-sensitive)
            if (location && std::string(location).length() > 0) {
                query_builder << "locationName" << bsoncxx::types::b_regex{std::string(location), "i"};
            }

            //fiunalize query and log
            auto query_doc = query_builder << bsoncxx::builder::stream::finalize;
            std::cout << "Final query: " << bsoncxx::to_json(query_doc.view()) << std::endl;

            //get reference to collection
            auto surfLocationColl = db["SurfLocation"];
            
            //create vector to store results
            std::vector<bsoncxx::document::value> results;
            
            //execute search
            auto cursor = surfLocationColl.find(query_doc.view());
            
            //loop through matching documents and add to results
            for (auto&& doc : cursor) {
                results.push_back(bsoncxx::document::value(doc));
                std::cout << "Found document: " << bsoncxx::to_json(doc) << std::endl;
            }

            //setup and manually construct JSON with results
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

            //release semaphore and log
            search_semaphore.release();
            std::cout << "Search semaphore released\n";

            //make response with results JSON and send
            auto res = crow::response(json_result);
            res.code = 200;
            res.add_header("Content-Type", "application/json");
            return res;
        } catch (const std::exception& e) {
            //release semaphore
            search_semaphore.release();
            
            //log sempahore released and error
            std::cout << "Search semaphore released (error)\n";
            std::string error_msg = std::string("{\"error\": \"") + e.what() + "\"}";
            
            //make response, add header, and send
            auto res = crow::response(500, error_msg);
            res.add_header("Content-Type", "application/json");
            return res;
        } });

    // ----- Endpoint 2.3: Get Location Details (Location Info & Posts) -----
    // Overload Prevention: Uses a semaphore to limit concurrent searches
    CROW_ROUTE(app, "/api/location-details").methods("GET"_method)([&db, &search_semaphore](const crow::request &req)
                                                                   {
        try {
            
            //acquire semaphore
            search_semaphore.acquire();
            
            //get location name from url parameters
            auto locationName = req.url_params.get("locationName");
            
            //if no name, release semaphore and return error response
            if (!locationName) {
                search_semaphore.release();
                return crow::response(400, "{\"error\": \"locationName parameter is required\"}");
            }
            
            //convert string type
            std::string location_str(locationName);

            // get collection reference from db
            auto surf_collection = db["SurfLocation"];
            
            //build location query for exact match
            bsoncxx::builder::stream::document location_query;
            location_query << "locationName" << location_str;
            
            //execute query to find location
            auto location_cursor = surf_collection.find(location_query.view());
            
            //loop through and store location in a vector
            std::vector<bsoncxx::document::value> location_results;
            for (auto&& doc : location_cursor) {
                location_results.push_back(bsoncxx::document::value(doc));
            }
            
            //if no location found, return error response
            if (location_results.empty()) {
                search_semaphore.release();
                return crow::response(404, "{\"error\": \"Location not found\"}");
            }

            // get posts collection from db
            auto post_collection = db["Post"];
            
            //setup exact location match query
            bsoncxx::builder::stream::document post_query;
            post_query << "locationName" << location_str;
            
            //execute query
            auto post_cursor = post_collection.find(post_query.view());
            
            //loop through and store post in vector
            std::vector<bsoncxx::document::value> post_results;
            for (auto&& doc : post_cursor) {
                post_results.push_back(bsoncxx::document::value(doc));
            }

            // combine location and post vectors
            std::vector<bsoncxx::document::value> combined_results;
            combined_results.insert(combined_results.end(), location_results.begin(), location_results.end());
            combined_results.insert(combined_results.end(), post_results.begin(), post_results.end());

            // build JSON from combined vector
            std::string json_result = "[";
            for (size_t i = 0; i < combined_results.size(); ++i) {
                json_result += bsoncxx::to_json(combined_results[i]);
                if (i < combined_results.size() - 1) {
                    json_result += ",";
                }
            }
            json_result += "]";

            //release semaphore
            search_semaphore.release();

            //make and send response with JSON results
            auto res = crow::response(json_result);
            res.code = 200;
            res.add_header("Content-Type", "application/json");
            return res;
        } catch (const std::exception& e) {
            //release semaphore
            search_semaphore.release();
            
            //setup and send error response
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
    //parse JSON request body, return error if not there
    auto body = crow::json::load(req.body);
    if (!body) {
        return crow::response(400, "{\"success\": false, \"message\": \"Invalid JSON\"}");
    }
    
    // get authenticated user info from middleware and log
    std::string user_id = req.get_header_value("X-User-ID");
    std::string username = req.get_header_value("X-Username");
    std::cout << "Protected create-post by user: " << username << " (ID: " << user_id << ")" << std::endl;
    
    // extract data before passing to async thread
    std::string username_value = body["username"].s();
    std::string locationName = body["locationName"].s();
    std::string description = body["description"].s();
    
    // use std::async to schedule the database operations on a separate thread
    auto futureResult = std::async(std::launch::async, [&db, username_value, locationName, description]() -> std::pair<bool, std::string> {
        try {
            // get collection reference
            auto post_collection = db["Post"];
            
            //try to insert post
            auto insert_result = post_collection.insert_one(
                bsoncxx::builder::stream::document{}
                    << "userId" << username_value
                    << "locationName" << locationName
                    << "description" << description
                    << "timestamp" << bsoncxx::types::b_date(std::chrono::system_clock::now())
                    << "TotalComments" << 0
                    << bsoncxx::builder::stream::finalize
            );
            
            //if unable to insert, return fail response
            if (!insert_result) {
                return {false, "Failed to insert post"};
            }
            
            // increment the postCount in the corresponding SurfLocation 
            //get collection reference
            auto location_collection = db["SurfLocation"];
            
            //update postCount ($inc means update field)
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
                //log post correct, but update fail
                std::cerr << "Warning: Created post but failed to update location post count for: " 
                          << locationName << std::endl;
            } else {
                //log both correct
                std::cout << "Updated postCount for location: " << locationName 
                          << ". Modified count: " << update_result->modified_count() << std::endl;
            }
            
            //return true response
            return {true, "Post created successfully"};
        } catch (const std::exception& e) {
            //log and return error response
            std::cerr << "Error creating post: " << e.what() << std::endl;
            return {false, e.what()};
        }
    });
    
    //wait for the async operation to complete and get the result
    auto [success, message] = futureResult.get();
    
    if (success) {
        //return success message
        return crow::response(200, "{\"success\": true, \"message\": \"" + message + "\"}");
    } else {
        //return fail message
        return crow::response(500, "{\"success\": false, \"message\": \"" + message + "\"}");
    } });

    // =========================================================================
    // Group 4: Comment Endpoints
    // =========================================================================

    // ----- Endpoint 4.1: Create Comment -----
    CROW_ROUTE(app, "/api/protected/create-comment").methods("POST"_method)([&db, &comment_creation_mutex](const crow::request &req)
                                                                            {
    //get JSON from request body
    auto body = crow::json::load(req.body);
    
    //if no body, return error resopnse
    if (!body) {
        return crow::response(400, "Invalid JSON");
    }
    
    // get authenticated user info from middleware part and log
    std::string user_id = req.get_header_value("X-User-ID");
    std::string username = req.get_header_value("X-Username");
    std::cout << "Protected create-comment by user: " << username << " (ID: " << user_id << ")" << std::endl;
    
    try {
        // lock the mutex to ensure thread safety during comment creation
        std::lock_guard<std::mutex> lock(comment_creation_mutex);
        
        // CRITICAL SECTION starts
        
        // get referebes for both collections
        auto comments_collection = db["Comments"];
        auto posts_collection = db["Post"];
        
        // insert comment
        auto insert_result = comments_collection.insert_one(
            bsoncxx::builder::stream::document{}
                << "postId" << body["postId"].s()
                << "userId" << body["userId"].s()
                << "commentDescription" << body["description"].s()
                << "timestamp" << bsoncxx::types::b_date(std::chrono::system_clock::now())
                << "TotalLikes" << 0
                << bsoncxx::builder::stream::finalize
        );
        
        //if couldn't insert, send error response
        if (!insert_result) {
            return crow::response(500, "{\"success\": false, \"message\": \"Failed to create comment\"}");
        }
        
        // increment the TotalComments counter in the Post
        std::string postId = body["postId"].s();
        
        // setup query
        bsoncxx::builder::stream::document post_query;
        
        // convert postId from ObjectId to string 
        if (postId.length() == 24 && std::all_of(postId.begin(), postId.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        })) {
            try {
                //try to convert
                bsoncxx::oid oid(postId);
                post_query << "_id" << oid;
            } catch (const std::exception& e) {
                //if conversion fails, make it stirng this way
                post_query << "_id" << postId;
            }
        } else {
            // use as string
            post_query << "_id" << postId;
        }
        
        //update TotalComment count by 1 ($inc)
        auto update_result = posts_collection.update_one(
            post_query.view(),
            bsoncxx::builder::stream::document{} 
                << "$inc" << bsoncxx::builder::stream::open_document
                    << "TotalComments" << 1
                << bsoncxx::builder::stream::close_document 
                << bsoncxx::builder::stream::finalize
        );
        
        // CRITICAL SECTION ends (lock is automatically released)
        
        //if couldn't update
        if (!update_result) {
            //log what happened (couldnt update)
            std::cerr << "Warning: Created comment but failed to update post comment count." << std::endl;
        } else {
            //log what happened (did update)
            std::cout << "Updated TotalComments for post " << postId 
                      << ". Modified count: " << update_result->modified_count() << std::endl;
        }
        
        //send success response
        return crow::response(200, "{\"success\": true, \"message\": \"Comment created successfully\"}");
    } catch (const std::exception& e) {
        //log and send error
        std::cerr << "Error creating comment: " << e.what() << std::endl;
        return crow::response(500, std::string("{\"success\": false, \"message\": \"") + e.what() + "\"}");
    } });

    // ----- Endpoint 4.2: Get Post Comments -----
    // Explicit Multi/Thread Scheduling: Also uses std::async to retrieve and process comments in a separate thread
    // ensures main thread doesn't get blocked with slow db
    CROW_ROUTE(app, "/api/post-comments").methods("GET"_method)([&db](const crow::request &req)
                                                                {
        //get value from parameter in URL
        auto postId = req.url_params.get("postId");
        
        //if no parameter, respond with error
        if (!postId) {
            return crow::response(400, "Missing postId parameter");
        }
        
        // use std::async to schedule the retrieval and processing of comments (seperate thread, async)
        auto futureResult = std::async(std::launch::async, [&db, postId]() -> crow::response {
            //refrence to comments colleciton
            auto comments_collection = db["Comments"];
            
            //settup query for matching postId
            bsoncxx::builder::stream::document query_builder;
            query_builder << "postId" << postId;
            
            //ececute query
            auto cursor = comments_collection.find(query_builder.view());
            
            //prepare JSON responses
            crow::json::wvalue result;
            crow::json::wvalue::list comments_list;
            
            //loop through each comment
            for (auto&& doc : cursor) {
                //convert BSON to JSON
                std::string doc_str = bsoncxx::to_json(doc);
                
                //parse JSON into Crow Jon
                auto rdoc = crow::json::load(doc_str);
                
                //if parse failed, skip this object
                if (!rdoc)
                    continue;
                
                //move rdoc into writable JSon
                crow::json::wvalue doc_json = std::move(rdoc);
                
                //convert mongoDB id into simpler Id
                if (doc_json["_id"].t() == crow::json::type::Object) {
                    //extract old ID as string
                    std::string oid_dump = doc_json["_id"]["$oid"].dump();
                    
                    //remove surrounding quotes
                    if (!oid_dump.empty() && oid_dump.front() == '"' && oid_dump.back() == '"') {
                        oid_dump = oid_dump.substr(1, oid_dump.size() - 2);
                    }
                    
                    //add new field in JSON with string value
                    doc_json["commentId"] = std::move(crow::json::wvalue(oid_dump));
                }
                //push comment info into list
                comments_list.push_back(doc_json);
            }
            //set comments field in result (which we send)
            result["comments"] = std::move(comments_list);
            
            //create and send response
            crow::response res(result);
            res.code = 200;
            res.add_header("Content-Type", "application/json");
            return res;
        });
        
        //wait for async to complete and return it's result (blocks new thread until db is complete)
        return futureResult.get(); });

    // ----- Endpoint 4.3: Like Comment -----
    CROW_ROUTE(app, "/api/protected/like-comment").methods("POST"_method)([&db, &like_semaphore](const crow::request &req)
                                                                          {
      //get JSON body from request
      auto body = crow::json::load(req.body);
      
      //if error with JSON, respond with error
      if (!body) {
        return crow::response(400, "Invalid JSON");
      }
      
      // get authenticated user info from request headers and middleware, log it
      std::string user_id = req.get_header_value("X-User-ID");
      std::string username = req.get_header_value("X-Username");
      std::cout << "Protected like-comment by user: " << username << " (ID: " << user_id << ")" << std::endl;
      
      try {
        // acquire semaphore and log
        like_semaphore.acquire();
        std::cout << "Like semaphore acquired for comment by user: " << username << std::endl;
        
        //get collection reference
        auto comments_collection = db["Comments"];
        
        //gets comment id from body
        std::string commentIdStr = body["commentId"].s();
        
        //convert into BSON
        bsoncxx::oid commentId(commentIdStr);
        
        //perform update on database (increased by 1 - using inc again)
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
        
        // release the semaphore and log
        like_semaphore.release();
        std::cout << "Like semaphore released for comment by user: " << username << std::endl;
        
        //if couldn't update, respond with error
        if (!update_result) {
          return crow::response(500, "{\"success\": false, \"message\": \"Failed to update comment like count\"}");
        }
        
        //if could update, respond with success
        return crow::response(200, "{\"success\": true, \"message\": \"Comment liked successfully\"}");
      } catch (const std::exception& e) {
        // release semaphore, even if error
        like_semaphore.release();
        
        //log semaphore release
        std::cout << "Like semaphore released (error) for comment by user: " << username << std::endl;
        
        //send response error
        return crow::response(500, std::string("Error: ") + e.what());
      } });

    // start server
    std::cout
        << "Starting server on port " << port << "...\n";
    app.port(port).multithreaded().run();

    // never actually get here
    return 0;
}