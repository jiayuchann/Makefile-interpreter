# Makefile-interpreter

Similar to the make command in Linux, the program tries to "make" every file reffered to through the command-line arguments. Instead of reading the file named "makefile", it reads the file named "fakefile". An example format of the fakefile is in the repository. Threading was used for execution of commands, in which arguments, pipes and redirections were manually handled, with error checking such as redirection of infinite number of outputs. Threading also waits for the completion of commands.  
