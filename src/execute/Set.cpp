#include <afina/Storage.h>
#include <afina/execute/Set.h>

#include <iostream>

namespace Afina {
namespace Execute {

// memcached protocol: "set" means "store this data".
void Set::Execute(Storage &storage, const std::string &args, std::string &out) {
    // KOCTblLb: network will append '\r\n' to args, executer will delete them
    std::string args_mod = args.substr(0, args.size() - 2);
    std::cout << "Set(" << _key << "): " << args_mod << std::endl;
    storage.Put(_key, args_mod);
    out = "STORED";
}

} // namespace Execute
} // namespace Afina
