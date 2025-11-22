#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "parser.h"
#include "formatter.h"

typedef enum {
	MODE_FORMAT,    // Format in place
	MODE_CHECK,     // Check if formatted correctly
	MODE_DIFF,      // Show diff
} FormatMode;

static char* read_file(const char *filename) {
	FILE *file = fopen(filename, "r");
	if (file == NULL) {
		fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
		return NULL;
	}

	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);

	char *content = malloc(size + 1);
	if (content == NULL) {
		fprintf(stderr, "Error: Out of memory\n");
		fclose(file);
		return NULL;
	}

	size_t read_size = fread(content, 1, size, file);
	content[read_size] = '\0';
	fclose(file);

	return content;
}

static int write_file(const char *filename, const char *content) {
	FILE *file = fopen(filename, "w");
	if (file == NULL) {
		fprintf(stderr, "Error: Cannot write to file '%s'\n", filename);
		return 0;
	}

	fputs(content, file);
	fclose(file);
	return 1;
}

static void usage(void) {
	fprintf(stderr, "Usage: hemfmt [options] <file>\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  (none)        Format file in place\n");
	fprintf(stderr, "  --check       Check if file is formatted correctly\n");
	fprintf(stderr, "  --diff        Show what would change\n");
	fprintf(stderr, "  --help        Show this help message\n");
}

int main(int argc, char **argv) {
	if (argc < 2) {
		usage();
		return 1;
	}

	FormatMode mode = MODE_FORMAT;
	const char *filename = NULL;

	// Parse arguments
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--check") == 0) {
			mode = MODE_CHECK;
		} else if (strcmp(argv[i], "--diff") == 0) {
			mode = MODE_DIFF;
		} else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			usage();
			return 0;
		} else if (argv[i][0] == '-') {
			fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
			usage();
			return 1;
		} else {
			filename = argv[i];
		}
	}

	if (filename == NULL) {
		fprintf(stderr, "Error: No input file specified\n");
		usage();
		return 1;
	}

	// Read input file
	char *source = read_file(filename);
	if (source == NULL) {
		return 1;
	}

	// Lex and parse
	Lexer lexer;
	lexer_init(&lexer, source);

	Parser parser;
	parser_init(&parser, &lexer);

	int count = 0;
	Stmt **statements = parse_program(&parser, &count);

	if (statements == NULL || parser.had_error) {
		fprintf(stderr, "Error: Failed to parse file\n");
		free(source);
		if (statements != NULL) {
			for (int i = 0; i < count; i++) {
				stmt_free(statements[i]);
			}
			free(statements);
		}
		return 1;
	}

	// Format AST
	char *formatted = format_statements(statements, count);

	// Execute based on mode
	int exit_code = 0;

	switch (mode) {
		case MODE_FORMAT: {
			// Write formatted output
			if (!write_file(filename, formatted)) {
				exit_code = 1;
			} else {
				printf("Formatted %s\n", filename);
			}
			break;
		}

		case MODE_CHECK: {
			// Compare with original
			if (strcmp(source, formatted) == 0) {
				printf("%s is already formatted\n", filename);
			} else {
				printf("%s needs formatting\n", filename);
				exit_code = 1;
			}
			break;
		}

		case MODE_DIFF: {
			// Show diff (simple line-by-line comparison)
			if (strcmp(source, formatted) == 0) {
				printf("No changes needed for %s\n", filename);
			} else {
				printf("--- %s (original)\n", filename);
				printf("+++ %s (formatted)\n", filename);
				printf("\n%s", formatted);
			}
			break;
		}
	}

	// Cleanup
	free(source);
	free(formatted);
	for (int i = 0; i < count; i++) {
		stmt_free(statements[i]);
	}
	free(statements);

	return exit_code;
}
