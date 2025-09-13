/*
MTFKS: Multi-Threaded File Keyword/Regex Search
*/

// Base program dependencies
#include <iostream>
#include <fstream>
#include <chrono>
#include <filesystem>
#include <queue>
#include <optional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <algorithm>
#include <regex>

// Define the namespace
namespace fs = std::filesystem;

struct ThreadSafeQueue {
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
        std::unique_lock<std::mutex> ul(m);
        cv.wait(ul, [&]{ return finished || !q.empty(); });
        if (q.empty()) return std::nullopt;
        auto path = q.front();
        q.pop();
        return path;
    }

    void set_finished() {
        {
            std::lock_guard<std::mutex> lg(m);
            finished = true;
        }
        cv.notify_all();
    }
};

// Atomic Counter for Number of Files
std::atomic<size_t> n_files_scanned{0};
std::mutex out_m;

// Search Implementation (supports keyword or regex)
bool search_file(const fs::path& p, const std::string& pattern, bool use_regex) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return false;

    ifs.seekg(0, std::ios::end);
    std::streamsize size = ifs.tellg();
    if (size < 0) return false;

    ifs.seekg(0, std::ios::beg);
    std::string contents(static_cast<size_t>(size), '\0');

    if (!ifs.read(&contents[0], size)) return false;

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
        auto option = q.pop();
        if (!option.has_value()) break;
        const auto path = *option;

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

    queue.set_finished();
    for (auto& thread : threads) thread.join();

    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
    std::cout << "\nScanned " << n_files_scanned.load() << " files in " << ms <<"ms.\n";

    return 0;
}