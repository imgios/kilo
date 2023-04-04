# Structure:
# target: dependencies
# 	recipe
#
# In our case kilo is our target, while kilo.c is the dependency.
# Our recipe is the C compiler command to compile it:
# 	$(CC) is a variable that make expands to cc (the C Compiler) by default
#	kilo.c is the source file name
#	-o kilo defines the compiler output
# 	-Wall stands for all Warnings
#	-Wextra and -pedantic turn on even more warnings
#	-std=c99 specifies the C-standard versions used

kilo: kilo.c
	$(CC) kilo.c -o kilo -Wall -Wextra -pedantic -std=c99