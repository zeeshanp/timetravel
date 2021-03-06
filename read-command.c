#include "command.h"
#include "command-internals.h"
#include <error.h>
#include "alloc.h"
#include <stdlib.h>
#include <stdio.h>

/* STACK IMPLEMENTATION */
typedef struct stack* stack_t;
struct stack
{
	void** data; //2d array so we dont worry about sizeof()
	size_t count;
	size_t max;
};
stack_t stack_init()
{
	stack_t s = (stack_t) checked_malloc(sizeof(struct stack));
	s->max = 8;
	s->count = 0;
	s->data = (void**) checked_malloc(s->max * sizeof(void*));
	return s;
}
void stack_free(stack_t s)
{
	free(s->data);
	free(s);
}
void stack_push(stack_t s, void* element)
{
	if (s->count == s->max)
	s->data = (void**)checked_grow_alloc(s->data, &s->max);
	s->data[s->count] = element;
	s->count = s->count + 1;
}
void* stack_pop(stack_t s)
{
	s->count = s->count - 1;
	return s->data[s->count];
}
void* stack_top(stack_t s) // aka stack.peek()
{
	return s->data[s->count- 1];
}
size_t stack_count(stack_t s)
{
	return s->count;
}

/* COMMAND STREAM*/
typedef struct command_node* command_node_t;
struct command_node
{
	command_t command;
	command_node_t next;
};
struct command_stream
{
	command_node_t head;
};

int isValid(char c)
{
	return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '!' || c == '%' || c == '+' ||
		c == ',' || c == '-' || c == '.' ||
		c == '/' || c == ':' || c == '@' ||
		c == '^' || c == '_');
}


command_t allocate()
{
	command_t cmd = (command_t) checked_malloc(sizeof(struct command));
	cmd->input = 0;
	cmd->output = 0;
	return cmd;
}

void* error_msg(int* err)
{
	*err = 1;
	return 0;
}

//returns true if prec(a) > prec(b)
char precedence(command_t a, command_t b)
{
	char p;
	if (b->type == PIPE_COMMAND)
	{
		p = a->type != PIPE_COMMAND;
		return p;
	}
	else if (b->type == AND_COMMAND || b->type == OR_COMMAND)
	{
        p = a->type == SEQUENCE_COMMAND;
		return p;
	}

	return 0;
}

//take in a word and add it to token stream
size_t word(char** c, char* buffer, size_t* size, size_t* max)
{
	size_t read = 0;

    while (**c == ' ')
		(*c)++;
	
	while (isValid(**c))
	{
		if (*size == *max)
			buffer = (char*) checked_grow_alloc(buffer, max);

        buffer[*size] = (**c);
        read++;
        (*size)++;
        (*c)++;
    }

    return read;
}

//form simple commands at leaf of trees
command_t simplecommand(char** c, int* err)
{
    while (**c == ' ') (*c)++;

	if (**c == '#')
		while (**c && **c != '\n')
			(*c)++;
    
    command_t cmd = allocate();
    cmd->type = SIMPLE_COMMAND;
    cmd->input = 0;
    cmd->output = 0;   

    int count = 0;

    size_t size = 0; 
    size_t inputsize = 0;
    size_t outputsize = 0;
	
	size_t max = 256;
	size_t inputmax = 16;
	size_t outputmax = 16;

    char* buffer = (char*) checked_malloc(max * sizeof(char));
    char* inputbuffer = (char*) checked_malloc(inputmax * sizeof(char));
    char* outputbuffer = (char*) checked_malloc(outputmax * sizeof(char));


    while (1)
    {
        size_t len = word(c, buffer, &size, &max);
        if (len != 0)
		{
            buffer[size] = '\0';
            size++;
           count++;  
        }
        
        char ch = **c;
		//skip spaces
        if (ch == ' ') 
		{
            while (**c == ' ') (*c)++;
        }
		//if we come to redirect make new word for input
        else if (ch == '<')
        {
            if (size == 0)
				error_msg(err);
            
			(*c)++;
            size_t read = word(c, inputbuffer, &inputsize, &inputmax);
            
			if (read == 0)
				error_msg(err);
            
			inputbuffer[inputsize] = '\0';
            inputsize++;
            cmd->input = inputbuffer;
            
			//skip spaces
			while (**c == ' ')
				(*c)++;
        }
        else if (ch == '>') //make output word
        {
            if (size == 0)
				error_msg(err);
            
			(*c)++;
            size_t read = word(c, outputbuffer, &outputsize, &outputmax);
            
			if (read == 0)
				error_msg(err);
            
			outputbuffer[outputsize] = '\0';
            outputsize++;
            cmd->output = outputbuffer;
            
			while (**c == ' ')
				(*c)++;
        }
        else 
			break;
    }

    if (size == 0)
        return 0;

    buffer[size] = '\0';
    cmd->u.word = (char**) checked_malloc((count + 1) * sizeof(char*));
    char* current = buffer;
    cmd->u.word[0] = current;
    int i;
    
	for(i = 1; i < count; i++)
    {
        while(*current != '\0') 
            current++;

        current++;
        cmd->u.word[i] = current;
    }

    cmd->u.word[count] = 0;
    
	return cmd;
}
    
command_t rootcommand(char** c, char top, int* err)
{
    stack_t cmd_stack = stack_init();
    stack_t op_stack = stack_init();
    
    while (**c == ' ' || **c == '\n' || **c == '#')
	{
		if (**c == '#')
			while (**c && **c != '\n')
				(*c)++;
		(*c)++;
	}

    if (!**c)
		return 0;
    
	command_t cmd = simplecommand(c, err);
    
	if (*err)
		return error_msg(err);
    
	if (!cmd)
	{
        if (**c == '(') 
		{
            (*c)++;
            cmd = allocate();
            cmd->type = SUBSHELL_COMMAND;
            cmd->u.subshell_command = rootcommand(c, 0, err);
            
			if (!cmd->u.subshell_command)
				return error_msg(err);
        }
        else
			return error_msg(err);
    }

    stack_push(cmd_stack, cmd);
    
    while(1)
	 {
        if (**c == '#')
			while (**c && **c != '\n')
				(*c)++;
		
        char ch = **c;
        
        
        command_t o = allocate();
        if (ch == '&') 
		{
            (*c)++;
            if (*(*c)++ != '&') 
				return error_msg(err);
            o->type = AND_COMMAND;
        }
        else if (ch == '|') 
		{
            (*c)++;
            if (**c == '|') 
			{
                (*c)++;
                o->type = OR_COMMAND;
            }
            else 
			{
                o->type = PIPE_COMMAND;
            }
        }
        else if (ch == ';') 
		{
            (*c)++;
            o->type = SEQUENCE_COMMAND;
        }
        else if (ch == '\n') 
		{
            (*c)++;
            while (**c == ' ') (*c)++;
			if (**c == '#')
				while (**c && **c != '\n')
					(*c)++;
            
            if (!**c || **c == '\n') 
			{
                if (!top) return error_msg(err);
                break;
            }
            o->type = SEQUENCE_COMMAND;
        }
        else if (ch == ')') 
		{
            if (top)
				return error_msg(err);
            (*c)++;
            
			while (**c == ' ') 
				(*c)++;
            break;
        }
        else if (!ch) 
		{
            if (!top)
				return error_msg(err);
            break;
        }
        else 
			return error_msg(err);
        
       
        while (stack_count(op_stack) != 0 && !precedence((command_t)stack_top, o)) 
		{
            command_t t = (command_t)stack_pop(op_stack);

            if (stack_count(cmd_stack) < 2) 
				error(1, 0, "Not enough Operands");

            t->u.command[1] = (command_t) stack_pop(cmd_stack);
            t->u.command[0] = (command_t) stack_pop(cmd_stack);
            stack_push(cmd_stack, t);
        }
        stack_push(op_stack, o);
        
        
		while (**c == ' ' || **c == '\n' || **c == '#')
		{
			if (**c == '#')
				while (**c && **c != '\n')
					(*c)++;
			(*c)++;
		}
        
        command_t next = simplecommand(c, err);
        
		if (*err)
			return error_msg(err);
       
		if (!next) 
		{
            if (**c == '(') 
			{
                (*c)++;
                next = allocate();
                next->type = SUBSHELL_COMMAND;
                next->u.subshell_command = rootcommand(c, 0, err);
                if (!next->u.subshell_command)
					return error_msg(err);
            }
            else 
				return error_msg(err);
        }
        stack_push(cmd_stack, next);
    }
    
    
    while (stack_count(op_stack) != 0 && c) 
	{
        command_t t = (command_t)stack_pop(op_stack);

        if (stack_count(cmd_stack) < 2)
			error(1, 0, "Error: Operator w/o 2 operands");

        t->u.command[1] = (command_t)stack_pop(cmd_stack);
        t->u.command[0] = (command_t)stack_pop(cmd_stack);
        stack_push(cmd_stack, t);
    }

    if (stack_count(op_stack) != 0 || stack_count(cmd_stack) != 1)
		return error_msg(err);
    
    cmd = stack_pop(cmd_stack);
    
    stack_free(cmd_stack);
    stack_free(op_stack);
    
    return cmd;
}

command_stream_t make_command_stream (int (*get_next_byte) (void *), void *get_next_byte_argument)
{
    
    command_stream_t cs = (command_stream_t)checked_malloc(sizeof(struct command_stream));
    
  
	//read in file into buffer

    size_t size = 0;
	size_t max = 1024;

    char* buffer = (char*) checked_malloc(max * sizeof(char));
    
	while(1) 
	{
        int c = get_next_byte(get_next_byte_argument);

        if (size == max) 
            buffer = (char*) checked_grow_alloc(buffer, &max);
        if (c == -1) 
		{
            buffer[size++] = '\0';
            break;
        }
        buffer[size++] = (char)c;
    }
    
   
    char* ptr = buffer, *marker;
    int err = 0;
	cs->head = (command_node_t) checked_malloc(sizeof(struct command_node));
	cs->head->command = 0;
	cs->head->next = 0;
    cs->head->command = rootcommand(&ptr, 1, &err);
    //grab error code
	if (err)
	{
        int line = 1;
        for (marker = buffer; marker != ptr; marker++)
            line += *marker == '\n';
        fprintf(stderr, "%d: Error - Syntax\n", line);
        exit(1);
    }
    if (!cs->head->command)
        error(1, 0, "Error - No commands");
    command_node_t current = cs->head;
    while (1) 
	{
        command_t cmd = rootcommand(&ptr, 1, &err);

        if (err) 
		{
            int line = 1;
            for (marker = buffer; marker != ptr; marker++)
                line += *marker == '\n';
            fprintf(stderr, "%d: Error - Syntax\n", line);
            exit(1);
        }

        if (!cmd)
			break;

        current->next = (command_node_t) checked_malloc(sizeof(struct command_node));
		current->next->command = 0;
		current->next->next = 0;
        current->next->command = cmd;
        current = current->next;
    }
    
    free(buffer);

    return cs;
}

command_t read_command_stream (command_stream_t s)
{
    command_node_t curr = s->head;
    if (!curr)
		return 0;

	//move pointers further along in forest

    s->head = s->head->next;
    command_t cmd = curr->command;
    
	free(curr);
    
	return cmd;
}


