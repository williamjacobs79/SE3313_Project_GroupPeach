#include "crow_all.h"            // Crow framework header
#include <bsoncxx/json.hpp>      // For BSON/JSON conversion
#include <mongocxx/client.hpp>   // MongoDB C++ driver client
#include <mongocxx/instance.hpp> // MongoDB C++ driver instance
#include <mongocxx/uri.hpp>      // For MongoDB URI
#include <bsoncxx/builder/stream/document.hpp>  // For building BSON documents
#include <bsoncxx/types.hpp>     // For BSON types

#include <fstream>    // For file I/O
#include <sstream>    // For string streams
#include <cstdlib>    // For std::getenv, setenv
#include <iostream>   // For std::cerr, std::cout
#include <string>     // For std::string
#include <chrono>     // For std::chrono::system_clock

// Simple function to load .env file variables into environment variables.
void loadDotEnv(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "Warning: Could not open .env file at " << path << std::endl;
        return;
    }
    
    std::string line;
    while (std::getline(file, line))
    {
        // Trim whitespace at beginning and end (basic trimming)
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t");
        line = line.substr(start, end - start + 1);
        
        // Skip comments and empty lines
        if (line[0] == '#' || line.empty()) continue;
        
        // Split the line at the first '=' character
        size_t delim_pos = line.find('=');
        if (delim_pos == std::string::npos) continue;
        
        std::string key = line.substr(0, delim_pos);
        std::string value = line.substr(delim_pos + 1);
        
        // Remove any surrounding quotes from value (if any)
        if (!value.empty() && value.front() == '"' && value.back() == '"')
        {
            value = value.substr(1, value.size() - 2);
        }
        
        // Set the environment variable (overwrite if exists)
        setenv(key.c_str(), value.c_str(), 1);
    }
    file.close();
}

int main()
{
    // Load environment variables from .env
    loadDotEnv(".env");
    
    // Initialize MongoDB driver instance (only needed once per application)
    mongocxx::instance instance{};

    // Retrieve MongoDB connection info from environment variables.
    const char* mongo_uri_env = std::getenv("MONGO_URI");
    if (!mongo_uri_env)
    {
        std::cerr << "Error: MONGO_URI environment variable not set." << std::endl;
        return 1;
    }
    std::string mongo_uri(mongo_uri_env);

    const char* db_name_env = std::getenv("DATABASE");
    if (!db_name_env)
    {
        std::cerr << "Error: DATABASE environment variable not set." << std::endl;
        return 1;
    }
    std::string db_name(db_name_env);

    // Retrieve port, default to 3000 if not set.
    const char* port_env = std::getenv("PORT");
    int port = port_env ? std::stoi(port_env) : 3000;

    // Create a client connection to MongoDB Atlas.
    mongocxx::client client{mongocxx::uri{mongo_uri}};
    auto db = client[db_name];

    // Check if collections exist and create them if they don't
    try {
        // List all collections
        auto collections = db.list_collection_names();
        std::vector<std::string> existing_collections(collections.begin(), collections.end());

        // Collections we need
        std::vector<std::string> required_collections = {"SurfLocation", "Post", "Likes", "Comments"};

        // Create missing collections
        for (const auto& collection_name : required_collections) {
            if (std::find(existing_collections.begin(), existing_collections.end(), collection_name) == existing_collections.end()) {
                db.create_collection(collection_name);
                std::cout << "Created collection: " << collection_name << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error setting up collections: " << e.what() << std::endl;
    }

    // Set up Crow HTTP server.
    crow::SimpleApp app;

    // Route to test server connectivity.
    CROW_ROUTE(app, "/")
    ([](){
        return "C++ backend server is up and running!";
    });

    // New route for testing MongoDB connectivity with a mock table entry.
    CROW_ROUTE(app, "/api/mock")([&db](){
        // Access the "mockTable" collection.
        auto collection = db["mockTable"];

        // Build a mock document using the BSON stream builder.
        using bsoncxx::builder::stream::document;
        using bsoncxx::builder::stream::finalize;
        document doc{};
        doc << "name" << "Test Entry"
            << "value" << 123
            << "description" << "This is a mock table entry for testing connectivity"
            << "timestamp" << bsoncxx::types::b_date{std::chrono::system_clock::now()}
            << finalize;

        // Insert the document into the collection.
        auto result = collection.insert_one(doc.view());
        if(result)
        {
            std::string response_str = R"({"status": "success", "message": "Mock entry inserted."})";
            return crow::response(200, response_str);
        }
        else
        {
            std::string response_str = R"({"status": "error", "message": "Failed to insert mock entry."})";
            return crow::response(500, response_str);
        }
    });

    // Endpoint for surf locations with filtering
    CROW_ROUTE(app, "/api/surf-locations")
    .methods("GET"_method)
    ([&db](const crow::request& req) {
        try {
            // Simple find query without any filters or projection
            auto collection = db["SurfLocation"];
            auto cursor = collection.find({});

            // Convert results to JSON
            std::vector<bsoncxx::document::value> results;
            for (auto&& doc : cursor) {
                results.push_back(bsoncxx::document::value(doc));
            }

            // Convert to JSON string
            std::string json_result = "[";
            for (size_t i = 0; i < results.size(); ++i) {
                json_result += bsoncxx::to_json(results[i]);
                if (i < results.size() - 1) {
                    json_result += ",";
                }
            }
            json_result += "]";

            return crow::response(json_result);
        } catch (const std::exception& e) {
            std::string error_msg = std::string("Error: ") + e.what();
            return crow::response(500, error_msg);
        }
    });

    // Endpoint to show database structure
    CROW_ROUTE(app, "/api/db-structure")
    .methods("GET"_method)
    ([&db](const crow::request& req) {
        try {
            std::string result = "{\n";
            result += "  \"collections\": [\n";

            // Get all collections
            auto collections = db.list_collection_names();
            bool first_collection = true;

            for (const auto& collection_name : collections) {
                if (!first_collection) {
                    result += ",\n";
                }
                first_collection = false;

                result += "    {\n";
                result += "      \"name\": \"" + collection_name + "\",\n";
                result += "      \"documents\": [\n";

                // Get sample document from collection
                auto collection = db[collection_name];
                auto cursor = collection.find({});
                bool first_doc = true;

                for (auto&& doc : cursor) {
                    if (!first_doc) {
                        result += ",\n";
                    }
                    first_doc = false;
                    
                    // Convert document to JSON with all fields
                    std::string doc_json = bsoncxx::to_json(doc);
                    result += "        " + doc_json;
                }

                result += "\n      ]\n";
                result += "    }";
            }

            result += "\n  ]\n";
            result += "}";

            return crow::response(result);
        } catch (const std::exception& e) {
            std::string error_msg = std::string("Error: ") + e.what();
            return crow::response(500, error_msg);
        }
    });

    // Run the server on the specified port.
    app.port(port).multithreaded().run();

    return 0;
}
