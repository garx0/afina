#include <afina/Storage.h>
#include <afina/execute/Append.h>

#include <iostream>

namespace Afina {
namespace Execute {

// memcached protocol: "append" means "add this data to an existing key after existing data".
void Append::Execute(Storage &storage, const std::string &args, std::string &out) {
    // KOCTblLb: network will append '\r\n' to args, executer will delete them
    std::string args_mod = args.substr(0, args.size() - 2);
    std::cout << "Append(" << _key << ")" << args_mod << std::endl;
    std::string value;
    if (!storage.Get(_key, value)) {
        out.assign("NOT_STORED");
        return;
    }
    storage.Put(_key, value + args_mod);
    out.assign("STORED");
}

} // namespace Execute
} // namespace Afina
