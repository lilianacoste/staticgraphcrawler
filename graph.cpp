#include <iostream>
#include <string>
#include <queue>
#include <unordered_set>
#include <cstdio>
#include <cstdlib>
#include <curl/curl.h>
#include <stdexcept>
#include "rapidjson/error/error.h"
#include "rapidjson/reader.h"
#include <mutex>
#include <thread>
#include <vector>
#include <condition_variable>
#include <chrono>
#include <rapidjson/document.h>
using namespace std;
using namespace rapidjson;

// Global Mutex for Synchronization
mutex mtx;
const int MAX_THREADS = 8;

// Define the base URL for the API 
const string SERVICE_URL = "http://hollywood-graph-crawler.bridgesuncc.org/neighbors/";
#ifndef RAPIDJSON_PARSE_ERROR_NORETURN
#define RAPIDJSON_PARSE_ERROR_NORETURN(code, offset) \
    throw ParseException(code, #code, offset)
#endif

struct ParseException : std::runtime_error, rapidjson::ParseResult {
    ParseException(rapidjson::ParseErrorCode code, const char* msg, size_t offset) :
        std::runtime_error(msg),
        rapidjson::ParseResult(code, offset) {}
};

// Helper function to encode parts of the URL
string url_encode(CURL* curl, const string& input) {
    char* out = curl_easy_escape(curl, input.c_str(), input.size());
    string s = out ? out : "";
    curl_free(out);
    return s;
}

// Callback function for writing response data
size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

// Function to fetch neighbors using libcurl
string fetch_neighbors(CURL* curl, const string& node) {
    string url = SERVICE_URL + url_encode(curl, node);
    string response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    // Set a User-Agent header
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "User-Agent: C++-Client/1.0");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        cerr << "CURL error: " << curl_easy_strerror(res) << endl;
        return "{}";
    }

    return response;
}

// Function to parse JSON and extract neighbors
vector<string> get_neighbors(const string& json_str) {
    vector<string> neighbors;
    Document doc;
    doc.Parse(json_str.c_str());

    if (doc.HasParseError()) {
        cerr << "Error while parsing JSON: " << json_str << endl;
        return neighbors;  // Return empty vector on failure
    }

    if (doc.HasMember("neighbors") && doc["neighbors"].IsArray()) {
        for (const auto& neighbor : doc["neighbors"].GetArray()) {
            if (neighbor.IsString()) {
                neighbors.push_back(neighbor.GetString());
            }
        }
    }
    return neighbors;
}

    vector<vector<string>> bfs(CURL* curl, const string& start, int depth) {
    auto start_time = chrono::high_resolution_clock::now();
    vector<vector<string>> levels;
    unordered_set<string> visited;
    levels.push_back({start});
    visited.insert(start);

    for (int d = 0; d < depth; d++) {
        cout << "Starting level: " << d << endl;
        levels.push_back({});

        vector<thread> threads;
        int num_nodes = levels[d].size();
        int nodes_per_thread = (num_nodes + MAX_THREADS - 1) / MAX_THREADS;  // Round up division

        for (int i = 0; i < MAX_THREADS; i++) {
            int start_idx = i * nodes_per_thread;
            int end_idx = min((i + 1) * nodes_per_thread, num_nodes);

            if (start_idx >= num_nodes) break;  // Prevent extra threads from spawning

            threads.emplace_back([=, &visited, &levels, &curl]() {
                vector<string> local_neighbors;
                for (int j = start_idx; j < end_idx; j++) {
                    string node = levels[d][j];
                    vector<string> neighbors = get_neighbors(fetch_neighbors(curl, node));

                    lock_guard<mutex> lock(mtx);  // Mutex to prevent race conditions
                    for (const auto& neighbor : neighbors) {
                        if (visited.insert(neighbor).second) {
                            local_neighbors.push_back(neighbor);
                        }
                    }
                }

                lock_guard<mutex> lock(mtx);  // Lock before modifying levels
                levels[d + 1].insert(levels[d + 1].end(), local_neighbors.begin(), local_neighbors.end());
            });
        }

        for (auto& t : threads) {
            t.join();
        }
    }

    auto end_time = chrono::high_resolution_clock::now();
    chrono::duration<double> elapsed = end_time - start_time;
    cout << "Execution Time: " << elapsed.count() << " seconds" << endl;

    cout << "Visited Nodes:" << endl;
    for (const auto& level : levels) {
        for (const string& node : level) {
            cout << node << " ";
        }
        cout << endl;
    }

    return levels;
}



int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <node_name> <depth>\n";
        return 1;
    }

    string start_node = argv[1];
    int depth;
    try {
        depth = stoi(argv[2]);
    } catch (const exception& e) {
        cerr << "Error: Depth must be an integer.\n";
        return 1;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        cerr << "Failed to initialize CURL" << endl;
        return 1;
    }

    const auto start_time = chrono::steady_clock::now();

    for (const auto& n : bfs(curl, start_node, depth)) {
        for (const auto& node : n) {
            cout << "- " << node << "\n";
        }
        cout << "Total Nodes: " << n.size() << "\n";
    }

    const auto end_time = chrono::steady_clock::now();
    chrono::duration<double> elapsed_seconds = end_time - start_time;
    cout << "Time to crawl: " << elapsed_seconds.count() << "s\n";

    curl_easy_cleanup(curl);
    return 0;
}

