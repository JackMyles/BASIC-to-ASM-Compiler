#include "compile.h"

#include <stdio.h>
#include <stdlib.h>

size_t if_label_num = 0;
size_t while_label_num = 0;

size_t left_shift(int64_t value) {
    size_t k = 0;
    if (value == 0) {
        return 0;
    }
    while (value != 1) {
        if (value % 2 != 0) {
            return 0;
        }
        k++;
        value /= 2;
    }
    return k;
}

int64_t reduce(node_t *node) {
    if (node->type != NUM) {
        binary_node_t *binary_node = (binary_node_t *) node;
        int64_t left = reduce(binary_node->left);
        int64_t right = reduce(binary_node->right);

        if (binary_node->op == '+') {
            return (left + right);
        }
        else if (binary_node->op == '*') {
            return (left * right);
        }
        else if (binary_node->op == '-') {
            return (left - right);
        }
        else if (binary_node->op == '/') {
            return (left / right);
        }
    }
    return (((num_node_t *) node)->value);
}

bool is_constant(node_t *node) {
    if (node->type == NUM) {
        return true;
    }
    else if (node->type == BINARY_OP) {
        binary_node_t *binary_node = (binary_node_t *) node;
        if (binary_node->op == '=' || binary_node->op == '<' || binary_node->op == '>') {
            return false;
        }
        if (is_constant(binary_node->left) && is_constant(binary_node->right)) {
            return true;
        }
    }
    return false;
}

bool compile_ast(node_t *node) {
    switch (node->type) {
        case NUM: {
            num_node_t *num_node = (num_node_t *) node;
            printf("movq $%ld, %%rdi\n", num_node->value);
            return true;
        }
        case PRINT: {
            print_node_t *print_node = (print_node_t *) node;
            compile_ast(print_node->expr);
            printf("call print_int\n");
            return true;
        }
        case SEQUENCE: {
            sequence_node_t *sequence_node = (sequence_node_t *) node;
            for (size_t i = 0; i < sequence_node->statement_count; i++) {
                compile_ast(sequence_node->statements[i]);
            }
            return true;
        }
        case BINARY_OP: {
            binary_node_t *binary_node = (binary_node_t *) node;

            if (is_constant(node)) {
                int64_t value = reduce(node);
                printf("movq $%ld, %%rdi\n", value);
                return true;
            }

            if (binary_node->op == '*' && is_constant(binary_node->right)) {
                int64_t value = reduce((node_t *) binary_node->right);
                size_t ls = left_shift(value);
                if (ls != 0) {
                    compile_ast(binary_node->left);
                    printf("shl $%zu, %%rdi\n", ls);
                    return true;
                }
            }

            compile_ast(binary_node->right);
            printf("pushq %%rdi\n");        // push right onto stack
            compile_ast(binary_node->left); // left is stored in rdi
            printf("popq %%rsi\n");         // pop right into rsi

            if (binary_node->op == '+') {
                printf("addq %%rsi, %%rdi\n");
            }
            else if (binary_node->op == '*') {
                printf("imulq %%rsi, %%rdi\n");
            }
            else if (binary_node->op == '-') {
                printf("subq %%rsi, %%rdi\n");
            }
            else if (binary_node->op == '/') {
                printf("movq %%rdi, %%rax\n");
                printf("cqto\n");
                printf("idivq %%rsi\n");
                printf("movq %%rax, %%rdi\n");
            }
            else {
                printf("cmp %%rsi, %%rdi\n");
            }

            return true;
        }
        case VAR: {
            var_node_t *var_node = (var_node_t *) node;
            int64_t address = 8 * (var_node->name - 'A' + 1);
            printf("movq -0x%lx(%%rbp), %%rdi\n", address);
            return true;
        }
        case LET: {
            let_node_t *let_node = (let_node_t *) node;
            int64_t address = 8 * (let_node->var - 'A' + 1);
            compile_ast(let_node->value);
            printf("movq %%rdi, -0x%lx(%%rbp)\n", address);
            return true;
        }
        case IF: {
            size_t if_label_num_local = if_label_num++;
            if_node_t *if_node = (if_node_t *) node;
            binary_node_t *binary_node = if_node->condition;
            compile_ast((node_t *) binary_node);

            if (binary_node->op == '=') {
                printf("je IF_LBB%zu\n", if_label_num_local);
            }
            else if (binary_node->op == '<') {
                printf("jl IF_LBB%zu\n", if_label_num_local);
            }
            else if (binary_node->op == '>') {
                printf("jg IF_LBB%zu\n", if_label_num_local);
            }

            if (if_node->else_branch != NULL) {
                compile_ast(if_node->else_branch);
            }
            printf("jmp IF_END_LBB%zu\n", if_label_num_local);
            printf("IF_LBB%zu:\n", if_label_num_local);
            compile_ast(if_node->if_branch);
            printf("IF_END_LBB%zu:\n", if_label_num_local);

            return true;
        }
        case WHILE: {
            size_t while_label_num_local = while_label_num++;
            while_node_t *while_node = (while_node_t *) node;
            binary_node_t *binary_node = while_node->condition;
            printf("WHILE_LBB%zu:\n", while_label_num_local);
            compile_ast((node_t *) binary_node);

            if (binary_node->op == '=') {
                printf("jne WHILE_END_LBB%zu\n", while_label_num_local);
            }
            else if (binary_node->op == '<') {
                printf("jnl WHILE_END_LBB%zu\n", while_label_num_local);
            }
            else if (binary_node->op == '>') {
                printf("jng WHILE_END_LBB%zu\n", while_label_num_local);
            }

            compile_ast(while_node->body);
            printf("jmp WHILE_LBB%zu\n", while_label_num_local);
            printf("WHILE_END_LBB%zu:\n", while_label_num_local);
            return true;
        }
    }
    return false;
}
