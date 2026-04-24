

#include "allocator.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

TLSFAllocator::TLSFAllocator(std::size_t memoryPoolSize) : poolSize(memoryPoolSize) {
    memoryPool = std::malloc(poolSize);
    for (int i = 0; i < FLI_SIZE; ++i) {
        index.fliBitmap = 0;
        index.sliBitmaps[i] = 0;
        for (int j = 0; j < SLI_SIZE; ++j) {
            index.freeLists[i][j] = nullptr;
        }
    }
    initializeMemoryPool(poolSize);
}

TLSFAllocator::~TLSFAllocator() {
    std::free(memoryPool);
}

void TLSFAllocator::initializeMemoryPool(std::size_t size) {
    if (size < sizeof(FreeBlock)) return;
    FreeBlock* initialBlock = reinterpret_cast<FreeBlock*>(memoryPool);
    initialBlock->size = size;
    initialBlock->isFree = true;
    initialBlock->prevPhysBlock = nullptr;
    initialBlock->nextPhysBlock = nullptr;
    insertFreeBlock(initialBlock);
}

void* TLSFAllocator::allocate(std::size_t size) {
    std::size_t totalSize = size + sizeof(BlockHeader);
    if (totalSize < MIN_BLOCK_SIZE) totalSize = MIN_BLOCK_SIZE;

    FreeBlock* block = findSuitableBlock(totalSize);
    if (!block) return nullptr;

    removeFreeBlock(block);
    if (block->size >= totalSize + MIN_BLOCK_SIZE) {
        splitBlock(block, totalSize);
    }
    block->isFree = false;
    return block->data();
}

void TLSFAllocator::deallocate(void* ptr) {
    if (!ptr) return;
    BlockHeader* header = reinterpret_cast<BlockHeader*>(reinterpret_cast<char*>(ptr) - sizeof(BlockHeader));
    header->isFree = true;
    FreeBlock* freeBlock = static_cast<FreeBlock*>(header);
    mergeAdjacentFreeBlocks(freeBlock);
}

void* TLSFAllocator::getMemoryPoolStart() const {
    return memoryPool;
}

std::size_t TLSFAllocator::getMemoryPoolSize() const {
    return poolSize;
}

std::size_t TLSFAllocator::getMaxAvailableBlockSize() const {
    for (int i = FLI_SIZE - 1; i >= 0; --i) {
        if (index.fliBitmap & (1U << i)) {
            for (int j = SLI_SIZE - 1; j >= 0; --j) {
                if (index.sliBitmaps[i] & (1U << j)) {
                    return index.freeLists[i][j]->size - sizeof(BlockHeader);
                }
            }
        }
    }
    return 0;
}

void TLSFAllocator::splitBlock(FreeBlock* block, std::size_t size) {
    std::size_t remainingSize = block->size - size;
    FreeBlock* newBlock = reinterpret_cast<FreeBlock*>(reinterpret_cast<char*>(block) + size);
    newBlock->size = remainingSize;
    newBlock->isFree = true;
    newBlock->prevPhysBlock = block;
    newBlock->nextPhysBlock = block->nextPhysBlock;
    if (block->nextPhysBlock) {
        block->nextPhysBlock->prevPhysBlock = newBlock;
    }
    block->nextPhysBlock = newBlock;
    block->size = size;
    insertFreeBlock(newBlock);
}

void TLSFAllocator::mergeAdjacentFreeBlocks(FreeBlock* block) {
    // Merge with next
    if (block->nextPhysBlock && block->nextPhysBlock->isFree) {
        FreeBlock* next = static_cast<FreeBlock*>(block->nextPhysBlock);
        removeFreeBlock(next);
        block->size += next->size;
        block->nextPhysBlock = next->nextPhysBlock;
        if (next->nextPhysBlock) {
            next->nextPhysBlock->prevPhysBlock = block;
        }
    }
    // Merge with prev
    if (block->prevPhysBlock && block->prevPhysBlock->isFree) {
        FreeBlock* prev = static_cast<FreeBlock*>(block->prevPhysBlock);
        removeFreeBlock(prev);
        prev->size += block->size;
        prev->nextPhysBlock = block->nextPhysBlock;
        if (block->nextPhysBlock) {
            block->nextPhysBlock->prevPhysBlock = prev;
        }
        block = prev;
    }
    insertFreeBlock(block);
}

void TLSFAllocator::mappingFunction(std::size_t size, int& fli, int& sli) {
    if (size == 0) {
        fli = 0; sli = 0; return;
    }
    fli = 0;
    std::size_t temp = size;
    while (temp >>= 1) fli++;
    
    if (fli < SLI_BITS) {
        sli = size - (1 << fli);
    } else {
        sli = (size - (1 << fli)) >> (fli - SLI_BITS);
    }
}

TLSFAllocator::FreeBlock* TLSFAllocator::findSuitableBlock(std::size_t size) {
    int fli, sli;
    std::size_t rounded_size = size;
    if (size >= (1 << SLI_BITS)) {
        int fli_temp = 0;
        std::size_t temp = size;
        while (temp >>= 1) fli_temp++;
        rounded_size = size + (1 << (fli_temp - SLI_BITS)) - 1;
    }
    mappingFunction(rounded_size, fli, sli);

    // Search in current fli, sli and above
    uint32_t sl_map = index.sliBitmaps[fli] & (~0U << sli);
    if (sl_map) {
        int found_sli = __builtin_ctz(sl_map);
        return index.freeLists[fli][found_sli];
    }
    
    uint32_t fl_map = index.fliBitmap & (~0U << (fli + 1));
    if (fl_map) {
        int found_fli = __builtin_ctz(fl_map);
        int found_sli = __builtin_ctz(index.sliBitmaps[found_fli]);
        return index.freeLists[found_fli][found_sli];
    }
    
    return nullptr;
}

void TLSFAllocator::insertFreeBlock(FreeBlock* block) {
    int fli, sli;
    mappingFunction(block->size, fli, sli);
    block->nextFree = index.freeLists[fli][sli];
    block->prevFree = nullptr;
    if (index.freeLists[fli][sli]) {
        index.freeLists[fli][sli]->prevFree = block;
    }
    index.freeLists[fli][sli] = block;
    index.fliBitmap |= (1U << fli);
    index.sliBitmaps[fli] |= (1U << sli);
}

void TLSFAllocator::removeFreeBlock(FreeBlock* block) {
    int fli, sli;
    mappingFunction(block->size, fli, sli);
    if (block->prevFree) {
        block->prevFree->nextFree = block->nextFree;
    } else {
        index.freeLists[fli][sli] = block->nextFree;
    }
    if (block->nextFree) {
        block->nextFree->prevFree = block->prevFree;
    }
    if (index.freeLists[fli][sli] == nullptr) {
        index.sliBitmaps[fli] &= ~(1U << sli);
        if (index.sliBitmaps[fli] == 0) {
            index.fliBitmap &= ~(1U << fli);
        }
    }
}

