#include <iostream>

namespace stdlib {
    void print() {}
}

void overloaded_func() {}
void overloaded_func(int) {}

class Logger {
public:
    void log(int x) {}
    void log(float x) {}
};

namespace outer {
    namespace inner {
        void inner_func() {
            Logger l;
            l.log(1); 
            stdlib::print(); // CALLS stdlib::print
            external_undefined_func(); // EXTERNAL edge
            overloaded_func(); // AMBIGUOUS_CALL to overloaded_func
        }
    }
}
