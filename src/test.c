#include <emscripten.h>

EMSCRIPTEN_KEEPALIVE
int add(int a, int b){
    return a+b;
}

int main(){
    return 0;
}