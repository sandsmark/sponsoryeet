EXECUTABLE=sponsoryeet
CXXFILES=$(wildcard *.cc)
OBJECTS=$(patsubst %.cc, %.o, $(CXXFILES))
LDFLAGS+=-ldl
CXXFLAGS+=-Wall -Wextra -pedantic -std=c++17 -fPIC -g

all: $(EXECUTABLE)

%.o: %.cc Makefile
	$(CXX) -MD -MP $(CXXFLAGS) -o $@ -c $<

DEPS=$(OBJECTS:.o=.d)
-include $(DEPS)

$(EXECUTABLE): $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS) $(CXXFLAGS)

clean:
	rm -f $(EXECUTABLE) $(OBJECTS) $(DEPS)

install: $(EXECUTABLE)
	install -D -m755 $(EXECUTABLE) $(DESTDIR)/usr/bin/$(EXECUTABLE)
