#pragma once

#include <string>
#include <functional>

namespace ter {

class Core {
public:
    Core();
    ~Core();

    bool init();
    void run();
    void shutdown();

private:
    bool running_{false};
};

} // namespace ter
