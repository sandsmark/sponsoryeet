EXECUTABLE=sponsoryeet
PROTOFILES=$(wildcard *.proto)
CXXFILES=$(wildcard *.cc)
PBHFILES=$(patsubst %.proto, %.pb.h, $(PROTOFILES))
PBCXXFILES=$(patsubst %.proto, %.pb.cc, $(PROTOFILES))
CXXFILES+=$(PBCXXFILES)
OBJECTS=$(patsubst %.cc, %.o, $(CXXFILES))
LDFLAGS+=-lprotobuf -lssl
CXXFLAGS+=-Wall -Wextra -pedantic -std=c++17 -fPIC -g

all: $(PBHFILES) $(EXECUTABLE)

%.o: %.cc Makefile $(PBHFILES)
	$(CXX) -MD -MP $(CXXFLAGS) -o $@ -c $<

%.pb.cc \
%.pb.h: %.proto Makefile
	protoc --cpp_out . $<

DEPS=$(OBJECTS:.o=.d)
-include $(DEPS)

$(EXECUTABLE): $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS) $(CXXFLAGS)

clean:
	rm -f $(EXECUTABLE) $(OBJECTS) $(DEPS) $(PBCXXFILES) $(PBHFILES)

install: $(EXECUTABLE)
	install -D -m755 $(EXECUTABLE) $(DESTDIR)/usr/bin/$(EXECUTABLE)
