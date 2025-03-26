#include <string>
#include "rapidjson/document.h"
#include <iostream>
#include <curl/curl.h>
#include <vector>
#include <string>
#include <chrono>
#include <unordered_set>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>

using namespace std;
using namespace rapidjson;

const string BASE_URL = "http://hollywood-graph-crawler.bridgesuncc.org/neighbors/";

size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

vector<string> fetch_neighbors(const string& node) {
    CURL* curl;
    CURLcode res;
    string url = BASE_URL + node;
    string response;

    curl = curl_easy_init();
    if (!curl) {
        cerr << "Curl initialization failed" << endl;
        return {};
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        cerr << "Curl request failed" << endl;
        return {};
    }

    Document doc;
    if (doc.Parse(response.c_str()).HasParseError()) {
        cerr << "Failed to parse JSON response" << endl;
        return {};
    }

    vector<string> neighbors;
    if (doc.HasMember("neighbors") && doc["neighbors"].IsArray()) {
        for (const auto& neighbor : doc["neighbors"].GetArray()) {
            neighbors.push_back(neighbor.GetString());
        }
    }
    return neighbors;
}

mutex mtx;
condition_variable cv;
const int MAX_THREADS = 8;

void bfs_traversal(const string& start_node, int depth) {
    auto start_time = chrono::high_resolution_clock::now();

    queue<pair<string, int>> q;
    unordered_set<string> visited;

    q.push({start_node, 0});
    visited.insert(start_node);

    cout << "BFS starting from: " << start_node << ", Depth limit: " << depth << endl;

    for (int current_depth = 0; current_depth < depth && !q.empty(); ++current_depth) {
        vector<string> nodes_at_level;
        queue<pair<string, int>> next_level;
        vector<thread> threads;

        // Extract all nodes at this level
        while (!q.empty()) {
            nodes_at_level.push_back(q.front().first);
            q.pop();
        }

        int num_nodes = nodes_at_level.size();
        int chunk_size = max(1, num_nodes / MAX_THREADS);

        for (int i = 0; i < num_nodes; i += chunk_size) {
            unique_lock<mutex> lck(mtx);
            cv.wait(lck, [&] { return threads.size() < MAX_THREADS; });

            threads.emplace_back([&, i]() {
                for (int j = i; j < i + chunk_size && j < num_nodes; ++j) {
                    string node = nodes_at_level[j];
                    vector<string> neighbors = fetch_neighbors(node);

                    lock_guard<mutex> lock(mtx);
                    for (const string& neighbor : neighbors) {
                        if (visited.find(neighbor) == visited.end()) {
                            visited.insert(neighbor);
                            next_level.push({neighbor, current_depth + 1});
                        }
                    }
                }
                cv.notify_one();
            });
        }

        for (thread& t : threads) {
            if (t.joinable()) t.join();
        }

        // Move to the next level
        while (!next_level.empty()) {
            q.push(next_level.front());
            next_level.pop();
        }
    }

    auto end_time = chrono::high_resolution_clock::now();
    chrono::duration<double> elapsed = end_time - start_time;
    cout << "Execution Time: " << elapsed.count() << " seconds" << endl;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <start_node> <depth>" << endl;
        return 1;
    }

    string start_node = argv[1];
    int depth = stoi(argv[2]);

    bfs_traversal(start_node, depth);
    return 0;
}
