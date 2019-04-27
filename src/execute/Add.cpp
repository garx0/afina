#include <afina/Storage.h>
#include <afina/execute/Add.h>

#include <iostream>

namespace Afina {
namespace Execute {

// memcached protocol:  "add" means "store this data, but only if the server *doesn't* already
// hold data for this key".
void Add::Execute(Storage &storage, const std::string &args, std::string &out) {
    // KOCTblLb: network will append '\r\n' to args, executer will delete them
    std::string args_mod = args.substr(0, args.size() - 2);
    std::cout << "Add(" << _key << ")" << args_mod << std::endl;
    out = storage.PutIfAbsent(_key, args_mod) ? "STORED" : "NOT_STORED";
}

} // namespace Execute
} // namespace Afina
