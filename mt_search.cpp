/* 
Multi-Threaded File Keyword Search Utility (mt_search.cpp)
Usage: ./mt_search <path> <keyword> <n_threads>
- <path>: directory path
- <keyword>: string keyword
- <n_threads>: number of threads to use
*/

// Import Dependencies
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

// Namespace
namespace fs = std::filesystem;

// Queue that our Worker will use
struct SafeQueue {
    std::queue<fs::path> q;
    std::mutex m;
    std::condition_variable cv;

    bool finished{false};

    void push(fs::path p) {
        {
            std::lock_guard<std::mutex> lg(m);
            q.push(std::move(p));
        }
        cv.notify_one();
    }

    std::optional<fs::path> pop() {
        // pop from the queue
        std::unique_lock<std::mutex> ul(m);
        
        cv.wait(ul, [&]{ return finished || !q.empty(); });

        if (q.empty()) {
            return std::nullopt;
        }

        auto p = q.front();
        q.pop();

        return p;
    }

    void set_finished() {
        // set the finished variable
        {
            std::lock_guard<std::mutex> lg(m);
            finished = true;
        }
        cv.notify_all();
    }
};

// Mutex
std::atomic<size_t> n_files_scanned{0};
std::mutex out_m;

// Search for the Keyword in the File
bool contains_keyword_file(const fs::path& p, const std::string& keyword) {
    // Input File Stream, Content String Buffer
    std::ifstream ifs(p, std::ios::binary);
    std::string contents;

    // Is the Input File Stream Empty?
    if (!ifs) {
        return false;
    }

    // Scan the entire file, capture its size
    ifs.seekg(0, std::ios::end);
    std::streamsize size = ifs.tellg();

    // Is the size invalid?
    if (size < 0) {
        return false;
    }

    // Resize the file buffer
    ifs.seekg(0, std::ios::beg);
    contents.resize(static_cast<size_t>(size));

    // File Contents Empty
    if (!ifs.read(&contents[0], size)) {
        return false;
    }

    return contents.find(keyword) != std::string::npos;
}

// Worker 
void worker(SafeQueue& q, const std::string& keyword) {
    while (true) {
        auto option = q.pop();

        if (!option.has_value()) {
            break;
        }

        const auto p = *option;

        try {
            if (fs::is_regular_file(p)) {
                ++n_files_scanned;
                if (contains_keyword_file(p, keyword)) {
                    std::lock_guard<std::mutex> lg(out_m);
                    std::cout << p << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lg(out_m);
            std::cerr << "[error]" << p << " : " << e.what() << std::endl;
        }
    }
}

// Main Driver Program
int main(int argc, char** argv) {
    // Sanity check the number of arguments passed
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <path> <keyword> <n_threads>\n";
        return 2;
    }

    fs::path root = argv[1];
    std::string keyword = argv[2];

    int num_threads = std::stoi(argv[3]);
    
    if (num_threads <= 0) {
        num_threads = 1;
    }

    SafeQueue queue;
    auto t0 = std::chrono::steady_clock::now();

    // Start all worker threads
    std::vector<std::thread> ths;
    for (int i=0; i < num_threads; ++i) {
        ths.emplace_back(worker, std::ref(queue), std::cref(keyword));
    }

    // Producer - walk filesystem and enqueue files
    try {
        for (auto const& dir_entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
            try {
                queue.push(dir_entry.path());
            } catch (...) {
                // Ignore
            }
        }    
    } catch (const std::exception& e) {
        std::cerr << "[walk error] " << e.what() << "\n";
    }

    // Signal completion and join threads
    queue.set_finished();
    for (auto& t : ths) {
        t.join();
    }

    // Summary
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
    std::cout << "\n Scanned Files: " << n_files_scanned.load() << " in " << ms << " ms\n";
    
    return 0;
}