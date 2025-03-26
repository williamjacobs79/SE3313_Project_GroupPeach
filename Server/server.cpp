#include "crow_all.h"            // Crow framework header
#include <bsoncxx/json.hpp>      // For BSON/JSON conversion
#include <mongocxx/client.hpp>   // MongoDB C++ driver client
#include <mongocxx/instance.hpp> // MongoDB C++ driver instance
#include <mongocxx/uri.hpp>      // For MongoDB URI
#include <bsoncxx/builder/stream/document.hpp>  // For building BSON documents
#include <bsoncxx/types.hpp>     // For BSON types

#include <iostream>  // For std::cerr
#include <string>    // For std::string
#include <chrono>    // For std::chrono::system_clock

int main()
{
    // Initialize MongoDB driver instance (only needed once per application)
    mongocxx::instance instance{};

    // Hard-coded MongoDB credentials
    std::string mongo_uri = 
        "mongodb+srv://wjacobsd:LeBrophieJames123@se3313grouppeach.zymaf1t.mongodb.net/"
        "?retryWrites=true&w=majority&appName=SE3313GroupPeach";
    std::string db_name = "SE3313GroupPeach";

    // Hard-coded port
    int port = 3001;

    // Create a client connection to MongoDB Atlas
    mongocxx::client client{mongocxx::uri{mongo_uri}};
    auto db = client[db_name];

    // Set up Crow HTTP server
    crow::SimpleApp app;

    // Route to test server connectivity
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

    // (Other routes like login, create-account, etc. can be added here later.)

    // Run the server on the specified port.
    app.port(port).multithreaded().run();

    return 0;
}
