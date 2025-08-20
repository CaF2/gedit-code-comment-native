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

CFLAGS = $(if $(PKG_CONF),$(shell pkg-config --cflags $(PKG_CONF))) -g -fPIC -Wall
CFLAGS += -MMD -MP

LDFLAGS = $(if $(PKG_CONF),$(shell pkg-config --libs $(PKG_CONF))) -shared

#CFLAGS += $(if $(NO_ASAN),,-fsanitize=address)
#LDFLAGS += $(if $(NO_ASAN),,-fsanitize=address)

RUN_COMMAND = gedit -s test.c

RUN_COMMAND_WEB = gedit -s test.html

###########

all: $(NAME).so

$(NAME).so: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.c.o: %.c
	$(CC) $< -c -o $@ $(CFLAGS)
	
run: all
	$(RUN_COMMAND)
	
runweb: all
	$(RUN_COMMAND_WEB)
	
gdb: all
	gdb --args $(RUN_COMMAND)

lldb: all
	lldb -- $(RUN_COMMAND)
	
valgrind: all
	valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all -v --log-file="$(NAME).valgrind.log" $(RUN_COMMAND)

-include $(OBJS:.o=.d)
