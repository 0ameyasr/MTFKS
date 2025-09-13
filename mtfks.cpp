/*
MTFKS: Multi-Threaded File Keyword/Regex Search
*/

// Base Dependencies
#include <iostream>
#include <fstream>
#include <filesystem>

// Concurrency Dependencies
#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

// Structures, Typing and Algorithms
#include <queue>
#include <optional>
#include <functional>
#include <algorithm>
#include <regex>

// Define the namespace
namespace fs = std::filesystem;

// Thread-Safe Queue Implementation
struct ThreadSafeQueue {
    // Queue container
    std::queue<fs::path> q;
    std::mutex m;
    std::condition_variable cv;
    bool finished{false};

    // Push a file path into the queue
    void push(fs::path p) {
        {
            // Use a Mutex Lock Guard to ensure no deadlocks occur
            std::lock_guard<std::mutex> lg(m);
            q.push(std::move(p));
        }

        // Notify a sleeping worker to take up a task
        cv.notify_one();
    }

    // Pop the object in the queue
    std::optional<fs::path> pop() {
        // Use a unique lock instead of a lock guard
        std::unique_lock<std::mutex> ul(m);
        cv.wait(ul, [&]{ return finished || !q.empty(); });

        // If the queue is empty, return a null object
        if (q.empty()) return std::nullopt;
        
        // Pop from the queue
        auto path = q.front();
        q.pop();

        return path;
    }

    // Set the finished conditional flag
    void set_finished() {
        {
            std::lock_guard<std::mutex> lg(m);
            finished = true;
        }

        // Notify all workers pre-merge, all tasks have been completed
        cv.notify_all();
    }
};

// Atomic Counter for Number of Files
std::atomic<size_t> n_files_scanned{0};
std::mutex out_m;

// Search Implementation (supports keyword or regex)
bool search_file(const fs::path& p, const std::string& pattern, bool use_regex) {
    // File Buffer
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return false;

    // Seek the size of the file
    ifs.seekg(0, std::ios::end);
    std::streamsize size = ifs.tellg();
    if (size < 0) return false;

    // Copy contents of the file
    ifs.seekg(0, std::ios::beg);
    std::string contents(static_cast<size_t>(size), '\0');

    // If empty, return False
    if (!ifs.read(&contents[0], size)) return false;

    // If we're using regex, parse it
    if (use_regex) {
        try {
            std::regex re(pattern);
            return std::regex_search(contents, re);
        } catch (std::regex_error& e) {
            return false;
        }
    } else {
        return contents.find(pattern) != std::string::npos;
    }
}

// Worker (Consumer)
void worker(ThreadSafeQueue& q, const std::string& pattern, bool use_regex) {
    while (true) {
        // Pop object in queue
        auto option = q.pop();
        if (!option.has_value()) break;
        const auto path = *option;

        // Search the file for the keyword/regex
        try {
            if (fs::is_regular_file(path)) {
                ++n_files_scanned;
                if (search_file(path, pattern, use_regex)) {
                    std::lock_guard<std::mutex> lg(out_m);
                    std::cout << path << std::endl;
                }
            }
        } catch (std::exception& e) {
            std::lock_guard<std::mutex> lg(out_m);
            std::cerr << "[error]" << path << ":" << e.what() << std::endl;
        }
    }
}

// Main Driver Program (Producer)
int main(int argc, char** argv) {
    // Handle arguments
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <keyword|regex> <path> <n_threads> <mode>\n";
        std::cerr << "mode: 0 = plain keyword, 1 = regex\n";
        return 2;
    }

    fs::path root = argv[2];
    std::string pattern = argv[1];
    int num_threads = std::stoi(argv[3]);
    bool use_regex = std::stoi(argv[4]) != 0;

    if (num_threads <= 0) num_threads = 1;

    // Initialize queue, start the timer
    ThreadSafeQueue queue;
    auto t0 = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i)
        threads.emplace_back(worker, std::ref(queue), std::cref(pattern), use_regex);

    try {
        for (auto const& dir_entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
            try {
                queue.push(dir_entry.path());
            } catch (...) {}
        }
    } catch (std::exception& e) {
        std::cerr << "[walk error]" << e.what() << "\n";
    }

    // All files parsed, end the timer, join all threads too
    queue.set_finished();
    for (auto& thread : threads) thread.join();

    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
    std::cout << "\nScanned " << n_files_scanned.load() << " files in " << ms <<"ms.\n";

    return 0;
}