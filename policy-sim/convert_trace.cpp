// Converts a page-granularity trace ("R|W <decimal page number>" per line)
// into the dense-ID format required by polsim, which indexes its page table
// directly with the address value. Page numbers are remapped to sequential
// IDs (0..N-1) in first-appearance order; access order and ops are preserved.
//
// Usage: ./convert_trace <trace_file> [sched_file]
//   -> writes <trace_file>.converted, and (if given) <sched_file>.converted
//      with the schedule's page numbers remapped through the same dictionary
#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

int main(int argc, char* argv[]) {
    if (argc != 2 && argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <trace_file> [sched_file]\n";
        return 1;
    }

    std::ifstream infile(argv[1]);
    if (!infile) {
        std::cerr << "Error: cannot open " << argv[1] << "\n";
        return 1;
    }

    std::string output_filename = std::string(argv[1]) + ".converted";
    std::ofstream outfile(output_filename);
    if (!outfile) {
        std::cerr << "Error: cannot write " << output_filename << "\n";
        return 1;
    }

    std::unordered_map<uint64_t, int> page_to_id;
    std::string op;
    uint64_t page;
    int next_id = 0;
    long long lines = 0;

    while (infile >> op >> page) {
        auto it = page_to_id.find(page);
        if (it == page_to_id.end())
            it = page_to_id.emplace(page, next_id++).first;
        outfile << op << " " << it->second << "\n";
        lines++;
    }

    std::cout << "Lines: " << lines << ", unique 4KB pages: " << next_id << "\n";
    std::cout << "Converted trace written to: " << output_filename << "\n";

    if (argc == 3) {
        std::ifstream sin(argv[2]);
        if (!sin) { std::cerr << "Error: cannot open " << argv[2] << "\n"; return 1; }
        std::ofstream sout(std::string(argv[2]) + ".converted");
        std::string tag; long long t; uint64_t pg; int tier, extra;
        long long kept = 0, dropped = 0;
        while (sin >> tag >> t >> pg >> tier >> extra) {
            auto it = page_to_id.find(pg);
            if (it == page_to_id.end()) { dropped++; continue; }
            sout << tag << " " << t << " " << it->second << " " << tier << " " << extra << "\n";
            kept++;
        }
        std::cout << "Converted schedule written (" << kept << " entries, "
                  << dropped << " dropped)\n";
    }
    return 0;
}
