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
#include <vector>
#include <thread>
#include <mutex>
#include <rapidjson/document.h>
using namespace std;
using namespace rapidjson;

mutex mtx; // Mutex to protect shared data

const string SERVICE_URL = "http://hollywood-graph-crawler.bridgesuncc.org/neighbors/";

// Helper function to encode URLs
string url_encode(CURL* curl, string input) {
  char* out = curl_easy_escape(curl, input.c_str(), input.size());
  string s = out;
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

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "User-Agent: C++-Client/1.0");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);

    return (res == CURLE_OK) ? response : "{}";
}

// Function to parse JSON and extract neighbors
vector<string> get_neighbors(const string& json_str) {
    vector<string> neighbors;
    Document doc;
    doc.Parse(json_str.c_str());

    if (doc.HasMember("neighbors") && doc["neighbors"].IsArray()) {
        for (const auto& neighbor : doc["neighbors"].GetArray())
            neighbors.push_back(neighbor.GetString());
    }

    return neighbors;
}

// Threaded function to handle node expansion and fetching neighbors
void expand_node(const string& node, CURL* curl, unordered_set<string>& visited, vector<string>& next_level) {
    try {
        vector<string> neighbors = get_neighbors(fetch_neighbors(curl, node));
        lock_guard<mutex> guard(mtx); // Locking mutex for thread safety
        for (const auto& neighbor : neighbors) {
            if (visited.find(neighbor) == visited.end()) {
                visited.insert(neighbor);
                next_level.push_back(neighbor);
            }
        }
    } catch (const exception& e) {
        cerr << "Error fetching neighbors for node: " << node << endl;
    }
}

// BFS with multi-threading and load balancing
vector<vector<string>> bfs(CURL* curl, const string& start, int depth, int max_threads) {
    vector<vector<string>> levels;
    unordered_set<string> visited;
    vector<string> current_level = {start};
    visited.insert(start);

    levels.push_back(current_level);

    for (int d = 0; d < depth; ++d) {
        vector<string> next_level;
        vector<thread> threads;

        int nodes_per_thread = (current_level.size() + max_threads - 1) / max_threads;
        int node_start = 0;

        for (int i = 0; i < max_threads; ++i) {
            int node_end = min((i + 1) * nodes_per_thread, (int)current_level.size());
            if (node_start < node_end) {
                threads.push_back(thread([&] {
                    for (int j = node_start; j < node_end; ++j) {
                        expand_node(current_level[j], curl, visited, next_level);
                    }
                }));
            }
            node_start = node_end;
        }

        // Join all threads
        for (auto& th : threads) {
            th.join();
        }

        levels.push_back(next_level);
        current_level = next_level;
    }

    return levels;
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        cerr << "Usage: " << argv[0] << " <node_name> <depth> <max_threads>\n";
        return 1;
    }

    string start_node = argv[1];
    int depth;
    int max_threads;
    
    try {
        depth = stoi(argv[2]);
        max_threads = stoi(argv[3]);
    } catch (const exception& e) {
        cerr << "Error: Depth and max_threads must be integers.\n";
        return 1;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        cerr << "Failed to initialize CURL" << endl;
        return -1;
    }

    const auto start = chrono::steady_clock::now();
    for (const auto& n : bfs(curl, start_node, depth, max_threads)) {
        for (const auto& node : n) {
            cout << "- " << node << "\n";
        }
        cout << "Level size: " << n.size() << "\n";
    }
    
    const auto finish = chrono::steady_clock::now();
    const chrono::duration<double> elapsed_seconds = finish - start;
    cout << "Time to crawl: " << elapsed_seconds.count() << "s\n";

    curl_easy_cleanup(curl);
    return 0;
}

