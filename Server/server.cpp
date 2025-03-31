#include "crow_all.h"
#include <bsoncxx/json.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>
#include <jwt-cpp/jwt.h>
#include <chrono>
#include <cstdlib>
#include <string>
#include <iostream>

// Secret key for JWT signing (in production, load this securely from an environment variable)
const std::string jwt_secret = "your_jwt_secret_key_here";

// Utility: Generate a JWT token for a given user
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

// Utility: Verify a JWT token and extract user_id and username
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

// JWT middleware for Crow; this middleware protects any route with "/api/protected"
struct JWTMiddleware {
    struct context {};

    void before_handle(crow::request& req, crow::response& res, context& ctx) {
        // Only check for JWT if the URL contains "/api/protected"
        if (req.url.find("/api/protected") != std::string::npos) {
            auto auth_header = req.get_header_value("Authorization");
            if (auth_header.empty() || auth_header.find("Bearer ") != 0) {
                res.code = 401;
                res.write("Unauthorized: Missing or invalid token");
                res.end();
                return;
            }
            std::string token = auth_header.substr(7); // Remove "Bearer " prefix
            std::string user_id, username;
            if (!verify_jwt(token, user_id, username)) {
                res.code = 401;
                res.write("Unauthorized: Token verification failed");
                res.end();
                return;
            }
            // Optionally, you can store user_id and username in the request context for later use.
        }
    }
    void after_handle(crow::request& req, crow::response& res, context& ctx) {
        // No post-processing required for now.
    }
};

int main() {
    // Initialize MongoDB driver instance (only once per application)
    mongocxx::instance instance{};

    // Retrieve MongoDB URI from environment variable or default to localhost
    const char* mongo_uri_env = std::getenv("MONGO_URI");
    std::string mongo_uri = mongo_uri_env ? mongo_uri_env : "mongodb://localhost:27017";

    mongocxx::client client{mongocxx::uri{mongo_uri}};
    auto db = client["EddieAikauDB"];

    // The "Accounts" collection will store user account details.
    auto accounts_collection = db["Accounts"];

    // Create the Crow app with JWT middleware
    crow::App<JWTMiddleware> app;

    // Route: Create Account (POST /api/create-account)
    CROW_ROUTE(app, "/api/create-account").methods("POST"_method)
    ([&accounts_collection](const crow::request& req){
        auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "Invalid JSON");
        }
        std::string username = body["username"].s();
        std::string password = body["password"].s();
        std::string email = body["email"].s();

        if(username.empty() || password.empty() || email.empty()) {
            return crow::response(400, "username, password, and email are required");
        }

        // Check if an account with the same username already exists
        auto filter = bsoncxx::builder::stream::document{} << "username" << username << bsoncxx::builder::stream::finalize;
        auto existing = accounts_collection.find_one(filter.view());
        if(existing) {
            return crow::response(409, "Account already exists");
        }

        // NOTE: In a production system, hash the password before storing it.
        auto insert_result = accounts_collection.insert_one(
            bsoncxx::builder::stream::document{} 
                << "username" << username 
                << "password" << password 
                << "email" << email 
                << bsoncxx::builder::stream::finalize
        );
        if(!insert_result) {
            return crow::response(500, "Failed to create account");
        }
        return crow::response(200, "Account created successfully");
    });

    #include <mutex>
    #include <unordered_map>
    
    // Global in-memory session store (for demonstration only)
    std::mutex session_mutex;
    std::unordered_map<std::string, int> active_sessions;
    
    CROW_ROUTE(app, "/api/login").methods("POST"_method)
    ([&accounts_collection](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "Invalid JSON");
        }
        std::string username = body["username"].s();
        std::string password = body["password"].s();
        
        if(username.empty() || password.empty()) {
            return crow::response(400, "Username and password are required");
        }
        
        // Query the database for the account
        auto filter = bsoncxx::builder::stream::document{} 
                        << "username" << username 
                        << "password" << password 
                        << bsoncxx::builder::stream::finalize;
        auto result = accounts_collection.find_one(filter.view());
        if(!result) {
            return crow::response(401, "Invalid username or password");
        }
        auto view = result->view();
        std::string user_id = view["_id"].get_oid().value.to_string();
    
        // Synchronize access to active_sessions
        {
            std::lock_guard<std::mutex> lock(session_mutex);
            int current_sessions = active_sessions[username]; // Defaults to 0 if key doesn't exist
            if (current_sessions >= 2) {
                return crow::response(403, "Maximum concurrent sessions reached for this account");
            }
            active_sessions[username] = current_sessions + 1;
        }
        
        // Generate a JWT token for the user
        std::string token = generate_jwt(user_id, username);
        
        crow::json::wvalue res_json;
        res_json["success"] = true;
        res_json["token"] = token;
        res_json["userId"] = user_id;
        res_json["username"] = username;
        return crow::response(res_json);
    });

   
    // Start the server on port 3000 (adjust as needed)
    app.port(3000).multithreaded().run();
    return 0;
}

