# MTFKS (Multi-Threaded File Keyword/Regex Search)

MTFKS is a lightweight C++ utility for recursively scanning directories to find files that contain a given keyword or regex pattern. It leverages multithreading to efficiently process large file sets using a producer–consumer model with a thread-safe queue.

## Features
1. Recursive file search in a specified directory.
2. Supports both plain text keywords and regular expressions.
3. Multi-threaded scanning for faster performance on large directories.
4. Thread-safe output of matching file paths.
5. Reports the total number of files scanned and execution time.
6. Almost 2.45x (Often 59% to 73%) times faster than recursive `grep`!

## How It Works
1. **Producer–Consumer Model:**
    - Producer: Traverses the directory tree and pushes file paths into a thread-safe queue.
    - Consumers (Workers): Multiple threads pop paths from the queue, read files, and search for the keyword or regex.

2. **Thread Safety:**
    - A ThreadSafeQueue ensures safe access for multiple threads using std::mutex and std::condition_variable.
    - std::atomic<size_t> tracks the number of files scanned.

3. **Search Modes:**
    - Keyword Mode: Simple string search.
    - Regex Mode: Full std::regex search with exception handling for invalid patterns.

4. **Requirements**
    - C++17 or later (for std::filesystem support)
    - Standard C++ library (no external dependencies)

## Building
```bash
g++ -std=c++17 -O2 -pthread mtfks.cpp -o mtfks
```

## Usage
```bash
./mtfks <keyword|regex> <path> <n_threads> <mode>
```

**Parameters**
- `<keyword|regex>` – The keyword or regex pattern to search for.
- `<path>` – Root directory to scan.
- `<n_threads>` – Number of worker threads.
- `<mode>` – 0 for plain keyword search, 1 for regex search.

## Examples

### **Keyword search:**
```bash
./mtfks "TODO" ./projects 4 0
```

### **Regex search:**
```bash
./mtfks "int\\s+main" ./projects 4 1
```
### Output
Matching file paths are printed to stdout.
After completion, a summary shows the total files scanned and runtime:

```bash
Scanned 132 files in 215ms.
```


## Notes
- Errors (e.g., permission denied) are printed to `stderr`.
- Large files are read into memory completely (within RAM). For extremely large files, consider reading in chunks.
- If the regex pattern is invalid, the file is skipped without crashing the program.
- `skip_permission_denied` prevents exceptions when access is denied to certain directories.