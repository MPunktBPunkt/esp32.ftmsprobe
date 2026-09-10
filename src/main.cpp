#include "app/App.h"

void setup() {
    App::instance().begin();
}

void loop() {
    App::instance().loop();
}
