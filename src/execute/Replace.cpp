#include <afina/Storage.h>
#include <afina/execute/Replace.h>

#include <iostream>

namespace Afina {
namespace Execute {

// memcached protocol:  "replace" means "store this data, but only if the server *does*
// already hold data for this key".

void Replace::Execute(Storage &storage, const std::string &args, std::string &out) {
    // KOCTblLb: network will append '\r\n' to args, executer will delete them
    std::string args_mod = args.substr(0, args.size() - 2);
    std::cout << "Replace(" << _key << "): " << args_mod << std::endl;
    std::string value;
    if (storage.Get(_key, value)) {
        storage.Set(_key, args_mod);
        out = "STORED";
    } else {
        out = "NOT_STORED";
    }
}

} // namespace Execute
} // namespace Afina
