TARGET = ployserv

CC = gcc
CFLAGS = -I$(IDIR)
# CFLAGS += -lm # Math lib

ODIR = output

SDIR =./src
SRC = $(wildcard $(SDIR)/*.c)
IDIR =./include
DEPS = $(wildcard $(SDIR)/*.h)

OBJ = $(patsubst $(SDIR)%.c, $(ODIR)%.o, $(SRC))

# Main target recipe
$(ODIR)/$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

# Rule to create output folder
$(ODIR):
	mkdir $@

# Make sure all object files are rebuilt when changing h and c files.
$(ODIR)/%.o: $(SDIR)/%.c $(ODIR) $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

.PHONY: clean echo

echo: # Debug
	@echo $(OBJ)

clean:
	@echo Cleaning up
	@rm -rf $(ODIR)

