

#include "allocator.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <map>

int main() {
    std::size_t poolSize;
    if (!(std::cin >> poolSize)) return 0;
    
    TLSFAllocator allocator(poolSize);
    std::map<int, void*> ptrMap;
    int nextId = 0;
    
    std::string cmd;
    while (std::cin >> cmd) {
        if (cmd == "alloc") {
            std::size_t size;
            std::cin >> size;
            void* ptr = allocator.allocate(size);
            if (ptr) {
                ptrMap[nextId] = ptr;
                std::cout << nextId << std::endl;
                nextId++;
            } else {
                std::cout << -1 << std::endl;
            }
        } else if (cmd == "free") {
            int id;
            std::cin >> id;
            if (ptrMap.count(id)) {
                allocator.deallocate(ptrMap[id]);
                ptrMap.erase(id);
            }
        } else if (cmd == "max") {
            std::cout << allocator.getMaxAvailableBlockSize() << std::endl;
        }
    }
    
    return 0;
}

