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
#include <algorithm>  // For std::transform

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

    // Print connection info
    std::cout << "Connected to database: " << db_name << std::endl;
    std::cout << "Using URI: " << mongo_uri << std::endl;

    // Check if collections exist and create them if they don't
    try {
        // List all collections
        auto collections = db.list_collection_names();
        std::vector<std::string> existing_collections(collections.begin(), collections.end());
        std::cout << "Existing collections: " << std::endl;
        for (const auto& name : existing_collections) {
            std::cout << "  - " << name << std::endl;
        }

        // Collections we need
        std::vector<std::string> required_collections = {"SurfLocation", "Post", "Likes", "Comments"};

        // Create missing collections
        for (const auto& collection_name : required_collections) {
            if (std::find(existing_collections.begin(), existing_collections.end(), collection_name) == existing_collections.end()) {
                db.create_collection(collection_name);
                std::cout << "Created collection: " << collection_name << std::endl;
            }
        }

        // Insert a test document into SurfLocation collection
        auto surf_location = db["SurfLocation"];
        
        // First, clear existing data
        auto delete_result = surf_location.delete_many({});
        std::cout << "Deleted " << delete_result->deleted_count() << " documents" << std::endl;
        
        // Create and insert test document
        bsoncxx::builder::stream::document test_doc{};
        test_doc << "locationName" << "Tofino"
                 << "breakType" << "Beach Break"
                 << "surfScore" << 7
                 << "countryName" << "Canada"
                 << "userId" << "test_user"
                 << "description" << "Famous surf spot in British Columbia"
                 << "coordinates" << bsoncxx::builder::stream::open_document
                 << "latitude" << 49.1538
                 << "longitude" << -125.9074
                 << bsoncxx::builder::stream::close_document;
        
        auto doc_value = test_doc << bsoncxx::builder::stream::finalize;
        
        // Print the document before insertion
        std::cout << "Attempting to insert document: " << bsoncxx::to_json(doc_value) << std::endl;
        
        // Insert the document using the view
        auto result = surf_location.insert_one(doc_value.view());
        if (result) {
            std::cout << "Successfully inserted test document" << std::endl;
        } else {
            std::cout << "Failed to insert test document" << std::endl;
        }

        // Print current contents of SurfLocation collection
        std::cout << "\nCurrent contents of SurfLocation collection:" << std::endl;
        auto cursor = surf_location.find({});
        for (auto&& doc : cursor) {
            std::cout << bsoncxx::to_json(doc) << std::endl;
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

    // Endpoint for surf locations with filtering
    CROW_ROUTE(app, "/api/surf-locations")
    .methods("GET"_method)
    ([&db](const crow::request& req) {
        try {
            // Get query parameters
            auto country = req.url_params.get("country");
            auto location = req.url_params.get("location");
            
            std::cout << "Received request with country: " << (country ? country : "none") 
                      << ", location: " << (location ? location : "none") << std::endl;

            // Build query document
            bsoncxx::builder::stream::document query{};
            bool has_valid_query = false;

            // Handle country parameter
            if (country) {
                std::string country_str(country);
                if (!country_str.empty()) {
                    query << "countryName" << bsoncxx::types::b_regex{country_str, "i"};
                    has_valid_query = true;
                    std::cout << "Added country filter: " << country_str << std::endl;
                }
            }

            // Handle location parameter
            if (location) {
                std::string location_str(location);
                if (!location_str.empty()) {
                    query << "locationName" << bsoncxx::types::b_regex{location_str, "i"};
                    has_valid_query = true;
                    std::cout << "Added location filter: " << location_str << std::endl;
                }
            }

            // Finalize the query document
            auto query_value = query << bsoncxx::builder::stream::finalize;
            std::cout << "Final query: " << bsoncxx::to_json(query_value) << std::endl;

            // Find documents
            auto collection = db["SurfLocation"];
            std::vector<bsoncxx::document::value> results;

            // Always perform query, but use empty query if no criteria provided
            auto cursor = collection.find(query_value.view());
            for (auto&& doc : cursor) {
                results.push_back(bsoncxx::document::value(doc));
                std::cout << "Found document: " << bsoncxx::to_json(doc) << std::endl;
            }

            // Print the locations being sent
            std::cout << "\nSending locations:" << std::endl;
            for (const auto& doc : results) {
                auto view = doc.view();
                std::cout << "Location: " << view["locationName"].get_string().value.to_string() 
                          << ", Country: " << view["countryName"].get_string().value.to_string() 
                          << ", Break Type: " << view["breakType"].get_string().value.to_string() 
                          << ", Surf Score: " << view["surfScore"].get_int32().value 
                          << ", Total Likes: " << view["TotalLikes"].get_int32().value 
                          << ", Total Comments: " << view["TotalComments"].get_int32().value << std::endl;
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

            std::cout << "Returning results: " << json_result << std::endl;
            
            // Create response with explicit status code and headers
            crow::response res;
            res.body = json_result;
            res.code = 200;
            res.add_header("Content-Type", "application/json");
            res.add_header("Access-Control-Allow-Origin", "*");
            return res;
        } catch (const std::exception& e) {
            std::string error_msg = std::string("Error: ") + e.what();
            std::cerr << error_msg << std::endl;
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
