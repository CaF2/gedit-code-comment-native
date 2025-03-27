# Copyright (c) 2025 Florian Evaldsson
# 
# This software is provided 'as-is', without any express or implied
# warranty. In no event will the authors be held liable for any damages
# arising from the use of this software.
# 
# Permission is granted to anyone to use this software for any purpose,
# including commercial applications, and to alter it and redistribute it
# freely, subject to the following restrictions:
# 
# 1. The origin of this software must not be misrepresented; you must not
#    claim that you wrote the original software. If you use this software
#    in a product, an acknowledgment in the product documentation would be
#    appreciated but is not required.
# 2. Altered source versions must be plainly marked as such, and must not be
#    misrepresented as being the original software.
# 3. This notice may not be removed or altered from any source distribution.

-include user.mk

CC = gcc

NAME = libcode-comment2

ARGS =

SRCS = gedit-code-comment.c

OBJS = $(SRCS:.c=.c.o)

PKG_CONF = gedit

CFLAGS = $(if $(PKG_CONF),$(shell pkg-config --cflags $(PKG_CONF))) -g -fPIC
CFLAGS += -MMD -MP

LDFLAGS = $(if $(PKG_CONF),$(shell pkg-config --libs $(PKG_CONF))) -shared

#CFLAGS += $(if $(NO_ASAN),,-fsanitize=address)
#LDFLAGS += $(if $(NO_ASAN),,-fsanitize=address)

###########


all: $(NAME).so

$(NAME).so: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

#all: $(NAME)

$(NAME): $(OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

%.c.o: %.c
	$(CC) $< -c -o $@ $(CFLAGS)
	
run: all
	./$(NAME) $(ARGS)
	
gdb: all
	gdb --args ./$(NAME) $(ARGS)

lldb: all
	lldb -- ./$(NAME) $(ARGS)
	
valgrind: all
	valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all -v --log-file="$(NAME).valgrind.log" ./$(NAME) $(ARGS)

-include $(OBJS:.o=.d)
