#include <cstdio>
#include <functional>
#include "CST816S.h"
#include "Wire.h"
FakeWire Wire;
static std::function<void()> handler;
static int irqLevel=HIGH;
void pinMode(int,int) {}
int digitalRead(int) {return irqLevel;}
void digitalWrite(int,int) {}
void delay(int) {}
void attachInterrupt(int,std::function<void()> callback,int) {handler=callback;}
int main() {
 int fails=0;
 auto check=[&](bool ok,const char *label) {std::printf("%s: %s\n",label,ok?"PASS":"FAIL");if(!ok)++fails;};
 CST816S touch(6,7,13,5);
 touch.listen(FALLING);
 check(!touch.hasPending(),"No fabricated touch on a high IRQ line");
 handler();
 // Display and IMU startup have not called touch.begin yet. The event must
 // survive unrelated work and remain available to motion-wake confirmation.
 check(touch.hasPending(),"Touch IRQ retained before sensor/display initialization");
 check(touch.hasPending(),"Pending touch survives repeated startup checks");
 touch.clearPending();
 check(!touch.hasPending(),"Accepted wake touch is consumed once");
 irqLevel=LOW;touch.listen(FALLING);
 check(touch.hasPending(),"IRQ already low when listener attaches is retained");
 irqLevel=HIGH;touch.begin(FALLING);
 check(!touch.hasPending(),"Controller reset does not replay the waking tap");
 handler();
 check(touch.available() && touch.data.points==1 && touch.data.x==120,"Normal touch report still processes after initialization");
 check(!touch.hasPending(),"Normal report consumes its IRQ");
 return fails?1:0;
}
