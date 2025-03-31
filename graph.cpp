#include <string>
#include <iostream>
#include <curl/curl.h>
#include <vector>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <cmath>
using namespace std;
using namespace rapidjson;

const string BASE_URL = "http://hollywood-graph-crawler.bridgesuncc.org/neighbors/";
const int MAX_THREADS = 8;  // Maximum concurrent threads

mutex mtx;  // Mutex to protect shared resources

size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

vector<string> fetch_neighbors(const string& node) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        cerr << "Curl initialization failed!" << endl;
        return {};
    }

    char* escaped_node = curl_easy_escape(curl, node.c_str(), node.length());
    if (!escaped_node) {
        cerr << "URL encoding failed!" << endl;
        curl_easy_cleanup(curl);
        return {};
    }

    string url = BASE_URL + escaped_node;
    curl_free(escaped_node);

    string response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "User-Agent: Crawler/1.0");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        cerr << "CURL error: " << curl_easy_strerror(res) << endl;
        return {};
    }

    Document doc;
    if (doc.Parse(response.c_str()).HasParseError()) {
        cerr << "Failed to parse JSON response: " << GetParseError_En(doc.GetParseError()) << endl;
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

void process_level(const vector<string>& current_level, unordered_set<string>& visited, vector<string>& next_level) {
    vector<thread> threads;
    mutex next_level_mutex;

    int num_threads = min(MAX_THREADS, (int)current_level.size());
    int nodes_per_thread = (int)ceil((double)current_level.size() / num_threads);

    for (int t = 0; t < num_threads; t++) {
        int start = t * nodes_per_thread;
        int end = min(start + nodes_per_thread, (int)current_level.size());

        threads.emplace_back([start, end, &current_level, &visited, &next_level, &next_level_mutex]() {
            vector<string> local_next;
            for (int i = start; i < end; i++) {
                vector<string> neighbors = fetch_neighbors(current_level[i]);

                lock_guard<mutex> lock(next_level_mutex);
                for (const string& neighbor : neighbors) {
                    if (visited.insert(neighbor).second) {
                        local_next.push_back(neighbor);
                    }
                }
            }

            lock_guard<mutex> lock(next_level_mutex);
            next_level.insert(next_level.end(), local_next.begin(), local_next.end());
        });
    }

    for (auto& th : threads) {
        th.join();
    }
}

void bfs_traversal(const string& start_node, int max_depth) {
    auto start_time = chrono::high_resolution_clock::now();

    unordered_set<string> visited;
    vector<vector<string>> levels(max_depth + 1);

    levels[0].push_back(start_node);
    visited.insert(start_node);

    for (int depth = 0; depth < max_depth; depth++) {
        if (levels[depth].empty()) break;

        vector<string> next_level;
        process_level(levels[depth], visited, next_level);
        levels[depth + 1] = move(next_level);
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

