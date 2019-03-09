#ifndef AFINA_STORAGE_SIMPLE_LRU_H
#define AFINA_STORAGE_SIMPLE_LRU_H

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <afina/Storage.h>

namespace Afina {
namespace Backend {

/**
 * # Map based implementation
 * That is NOT thread safe implementaiton!!
 */
class SimpleLRU : public Afina::Storage {
public:
    SimpleLRU(size_t max_size = 1024)
        : _max_size(max_size), _size(0) {}

    ~SimpleLRU() override {
        if(!_lru_index.empty()) {
            _Clear();
        };
    }

    // Implements Afina::Storage interface
    bool Put(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool PutIfAbsent(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Set(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Delete(const std::string &key) override;

    // Implements Afina::Storage interface
    bool Get(const std::string &key, std::string &value) override;

    // For debugging: prints index and list data for manual integrity checking
    void _PrintDebug(std::ostream& os);
private:
    // LRU cache node
    struct lru_node {
        ~lru_node() {
            if(next) {
                // all lru_node objects are allocated with "new"
                delete next.release();
            }
        }
        const std::string key;
        std::string value;
        lru_node* prev;
        std::unique_ptr<lru_node> next;
    };
    using lru_node = struct lru_node;

    //
    void _Clear();

    // Put new node without searching for key
    bool _PutNew(const std::string &key, const std::string &value);

    // Get value by node reference
    void _Get(std::reference_wrapper<lru_node> ref, std::string &value);

    // Set value by node reference
    bool _Set(std::reference_wrapper<lru_node> ref, const std::string &value);

    // Delete elements from tail while cache size > specified size
    void _ReduceToSize(std::size_t size);

    // Delete last element
    void _DeleteFromTail();

    // Move element to head
    void _MoveToHead(std::reference_wrapper<lru_node> ref);

    // Maximum number of bytes could be stored in this cache.
    // i.e all (keys+values) must be less the _max_size
    std::size_t _max_size;

    // Current number of bytes (all (keys+values))
    std::size_t _size;

    // Main storage of lru_nodes, elements in this list ordered descending by "freshness": in the head
    // element that wasn't used for longest time.
    //
    // List owns all nodes
    std::unique_ptr<lru_node> _lru_head;
    lru_node* _lru_tail;

    // Index of nodes from list above, allows fast random access to elements by lru_node#key
    std::map<std::reference_wrapper<const std::string>, std::reference_wrapper<lru_node>, std::less<std::string>> _lru_index;
};

} // namespace Backend
} // namespace Afina

#endif // AFINA_STORAGE_SIMPLE_LRU_H
