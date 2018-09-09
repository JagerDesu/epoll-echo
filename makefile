CC := g++
CFLAGS := -O0 -g -std=c++14 -Wall -Wextra -Iinclude/
LDFLAGS := -g 
LDLIBS  := -lpthread
TARGET := epoll-echo

BINEXT := 

BUILDDIR := bin
SOURCEDIR := src
OBJDIR := obj

include src/makefile

OBJS := $(addprefix $(OBJDIR)/, $(OBJS))

DEPS := $(OBJS:.o=.d)


# The main target
$(BUILDDIR)/$(TARGET)$(BINEXT): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@ $(LDLIBS)

clean:
	rm -f $(OBJS) $(DEPS) $(BUILDDIR)/$(TARGET)$(BINEXT) 

# makes sure the target dir exists
MKDIR = if [ ! -d $(dir $@) ]; then mkdir -p $(dir $@); fi

$(OBJDIR)/%.o: $(SOURCEDIR)/%.cpp
	@$(MKDIR)
	@echo Compiling $<
	@$(CC) $(CFLAGS) -c $< -MD -MT $@ -MF $(@:%o=%d) -o $@
