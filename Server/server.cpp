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
        // Trim whitespace at beginning and end
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t");
        line = line.substr(start, end - start + 1);
        
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;
        
        // Split line at first '='
        size_t delim_pos = line.find('=');
        if (delim_pos == std::string::npos) continue;
        std::string key = line.substr(0, delim_pos);
        std::string value = line.substr(delim_pos + 1);
        
        // Remove surrounding quotes if present
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
        if (req.method == crow::HTTPMethod::OPTIONS) {
            res.add_header("Access-Control-Allow-Origin", "*");
            res.add_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization, Accept");
            res.end();
        }
    }

    void after_handle(crow::request& req, crow::response& res, context& ctx) {
        res.add_header("Access-Control-Allow-Origin", "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization, Accept");
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
            // Optionally store user_id/username in context.
        }
    }
    void after_handle(crow::request& req, crow::response& res, context& ctx) {
        // No post-processing required.
    }
};

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main() {
    // Load environment variables from .env file
    loadDotEnv(".env");

    // Re-read environment variables
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

    // Initialize MongoDB driver instance and create client/DB reference
    mongocxx::instance instance{};
    mongocxx::client client{mongocxx::uri{mongo_uri}};
    auto db = client[db_name];

    // Prepare "Accounts" collection for user accounts
    auto accounts_collection = db["Accounts"];

    // Create a counting semaphore for search operations (limit 3 concurrent)
    std::counting_semaphore<3> search_semaphore(3);

    // In-memory session store for account logins
    std::mutex session_mutex;
    std::unordered_map<std::string, int> active_sessions;

    // Create Crow app with CORSMiddleware & JWTMiddleware
    crow::App<CORSMiddleware, JWTMiddleware> app;

    // -------------------------------------------------------------------------
    // Root endpoint: connectivity test
    // -------------------------------------------------------------------------
    CROW_ROUTE(app, "/").methods("GET"_method)
    ([]() {
        crow::json::wvalue res_json;
        res_json["message"] = "C++ backend server is up and running!";
        return crow::response(200, res_json);
    });

    // -------------------------------------------------------------------------
    // Database structure endpoint
    // -------------------------------------------------------------------------
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

    // -------------------------------------------------------------------------
    // Surf Locations (GET /api/surf-locations)
    // -------------------------------------------------------------------------
    CROW_ROUTE(app, "/api/surf-locations").methods("GET"_method)
    ([&db, &search_semaphore](const crow::request& req) {
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
                    if (i < results.size() - 1) json_result += ",";
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

    // -------------------------------------------------------------------------
    // Insert Surf Location (POST /api/insert-surf-location)
    // -------------------------------------------------------------------------
    CROW_ROUTE(app, "/api/insert-surf-location").methods("POST"_method)
    ([&db](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "Invalid JSON");
        }
        auto collection = db["SurfLocation"];
        try {
            auto insert_result = collection.insert_one(bsoncxx::builder::stream::document{}
                << "countryName" << body["countryName"].s()
                << "locationName" << body["locationName"].s()
                << "breakType" << body["breakType"].s()
                << "surfScore" << std::stoi(body["surfScore"].s())
                << "userId" << body["userId"].s()
                << bsoncxx::builder::stream::finalize);
            
            if (!insert_result) {
                return crow::response(500, "Insertion failed");
            }
            return crow::response(200, "Surf location inserted successfully");
        } catch (const std::exception& e) {
            return crow::response(500, std::string("Error: ") + e.what());
        }
    });

    // -------------------------------------------------------------------------
    // Create Account (POST /api/create-account)
    // -------------------------------------------------------------------------
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

    // -------------------------------------------------------------------------
    // Login (POST /api/login)
    // -------------------------------------------------------------------------
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
        {
            std::lock_guard<std::mutex> lock(session_mutex);
            int current_sessions = active_sessions[username];
            if (current_sessions >= 2) {
                response_json["success"] = false;
                response_json["message"] = "Maximum concurrent sessions reached for this account";
                return crow::response(403, response_json);
            }
            active_sessions[username] = current_sessions + 1;
        }
        std::string token = generate_jwt(user_id, username);
        response_json["success"] = true;
        response_json["token"] = token;
        response_json["userId"] = user_id;
        response_json["username"] = username;
        return crow::response(200, response_json);
    });

    // -------------------------------------------------------------------------
    // Run the server
    // -------------------------------------------------------------------------
    std::cout << "Starting server on port " << port << "...\n";
    app.port(port).multithreaded().run();
    return 0;
}
