#include "crow_all.h"            // Crow framework header (ensure it's in your include path)
#include <bsoncxx/json.hpp>      // For BSON/JSON conversion
#include <mongocxx/client.hpp>   // MongoDB C++ driver client
#include <mongocxx/instance.hpp> // MongoDB C++ driver instance
#include <mongocxx/uri.hpp>      // For MongoDB URI

int main()
{
    // Initialize MongoDB driver instance (only needed once in your application)
    mongocxx::instance instance{};

    // Create a client connection to MongoDB Atlas using your connection string
    // Replace <your MongoDB Atlas URI> with your actual connection URI.
    mongocxx::instance instance{};
    mongocxx::client client{
        mongocxx::uri{"mongodb+srv://wjacobsd:LeBrophieJames123@se3313grouppeach.zymaf1t.mongodb.net/?retryWrites=true&w=majority&appName=SE3313GroupPeach"}
};
auto db = client["SE3313GroupPeach"];


    // Set up Crow HTTP server
    crow::SimpleApp app;

    // Placeholder route for testing server connectivity.
    CROW_ROUTE(app, "/")
    ([](){
        return "C++ backend server is up and running!";
    });

    // You can add additional routes/endpoints here later.
    // For example:
    // CROW_ROUTE(app, "/api/login").methods("POST"_method)([&db](const crow::request& req){
    //     // Parse JSON from req.body, query MongoDB using db, etc.
    //     return crow::response(200, "Login endpoint not implemented yet.");
    // });

    // Run the server on port 3000
    app.port(3000).multithreaded().run();

    return 0;
}
