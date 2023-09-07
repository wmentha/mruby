#include <mruby.h>

#ifdef MRB_NO_STDIO
# error mruby-bin-mrbc conflicts 'MRB_NO_STDIO' in your build configuration
#endif

#include <stdlib.h>
#include <string.h>
#include <mruby/compile.h>
#include <mruby/dump.h>
#include <mruby/proc.h>
#include <mruby/internal.h>

#define RITEBIN_EXT ".mrb"
#define C_EXT       ".c"
#define C_HEAD_EXT  ".h"

struct mrbc_args {
  const char *prog;
  const char *outfile;
  const char *initname;
  char **argv;
  int argc;
  int idx;
  uint8_t line_size     : 8;
  mrb_bool check_syntax : 1;
  mrb_bool verbose      : 1;
  mrb_bool remove_lv    : 1;
  mrb_bool no_ext_ops   : 1;
  mrb_bool no_optimize  : 1;
  uint8_t flags         : 5;
};

static void
usage(const char *name)
{
  static const char *const usage_msg[] = {
  "switches:",
  "-c                  check syntax only",
  "-o<outfile>         place the output into <outfile>; required for multi-files; appropriate extension is appended",
  "-v                  print version number, then turn on verbose mode",
  "-g                  produce debugging information",
  "-B<symbol>          binary <symbol> output in C language format",
  "-S                  dump output as C struct (requires -B)",
  "-s                  define <symbol> as C static variable (requires -B)",
  "-H                  dump binary output with header file (requires -B)",
  "-8                  dump binary output as octal string (requires -B)",
  "--line-size<number> number of hex or octal values per line (min 1, max 255, default 16)",
  "--remove-lv         remove local variables",
  "--no-ext-ops        prohibit using OP_EXTs",
  "--no-optimize       disable peephole optimization",
  "--verbose           run at verbose mode",
  "--version           print the version",
  "--copyright         print the copyright",
  NULL
  };
  const char *const *p = usage_msg;

  printf("Usage: %s [switches] programfile...\n", name);
  while (*p)
    printf("  %s\n", *p++);
}

static char *
get_outfilename(mrb_state *mrb, char *infile, const char *ext)
{
  size_t ilen, flen, elen;
  char *outfile;
  char *p = NULL;

  ilen = strlen(infile);
  flen = ilen;
  if (*ext) {
    elen = strlen(ext);
    if ((p = strrchr(infile, '.'))) {
      ilen = p - infile;
    }
    flen += elen;
  }
  else {
    flen = ilen;
  }
  outfile = (char*)mrb_malloc(mrb, flen+1);
  strncpy(outfile, infile, ilen+1);
  if (p) {
    strncpy(outfile+ilen, ext, elen+1);
  }

  return outfile;
}

static int
parse_args(mrb_state *mrb, int argc, char **argv, struct mrbc_args *args)
{
  static const struct mrbc_args args_zero = { 0 };
  int i;

  *args = args_zero;
  args->argc = argc;
  args->argv = argv;
  args->prog = argv[0];

  for (i=1; i<argc; i++) {
    if (argv[i][0] == '-') {
      switch ((argv[i])[1]) {
      case 'o':
        if (args->outfile) {
          fprintf(stderr, "%s: an output file is already specified. (%s)\n",
                  args->prog, args->outfile);
          return -1;
        }
        if (argv[i][2] == '\0' && argv[i+1]) {
          i++;
          args->outfile = argv[i];
        }
        else {
          args->outfile = argv[i] + 2;
        }
        break;
      case 'S':
        args->flags |= MRB_DUMP_STRUCT;
        break;
      case 'B':
        if (argv[i][2] == '\0' && argv[i+1]) {
          i++;
          args->initname = argv[i];
        }
        else {
          args->initname = argv[i]+2;
        }
        if (*args->initname == '\0') {
          fprintf(stderr, "%s: function name is not specified.\n", args->prog);
          return -1;
        }
        break;
      case 'H':
        args->flags |= MRB_DUMP_HEADER;
        break;
      case '8':
        args->flags |= MRB_DUMP_OCTAL;
        break;
      case 'c':
        args->check_syntax = TRUE;
        break;
      case 'v':
        if (!args->verbose) mrb_show_version(mrb);
        args->verbose = TRUE;
        break;
      case 'g':
        args->flags |= MRB_DUMP_DEBUG_INFO;
        break;
      case 's':
        args->flags |= MRB_DUMP_STATIC;
        break;
      case 'E':
      case 'e':
        fprintf(stderr, "%s: -e/-E option no longer needed.\n", args->prog);
        break;
      case 'h':
        return -1;
      case '-':
        if (argv[i][1] == '\n') {
          return i;
        }
        if (strcmp(argv[i] + 2, "version") == 0) {
          mrb_show_version(mrb);
          exit(EXIT_SUCCESS);
        }
        else if (strcmp(argv[i] + 2, "verbose") == 0) {
          args->verbose = TRUE;
          break;
        }
        else if (strcmp(argv[i] + 2, "copyright") == 0) {
          mrb_show_copyright(mrb);
          exit(EXIT_SUCCESS);
        }
        else if (strcmp(argv[i] + 2, "remove-lv") == 0) {
          args->remove_lv = TRUE;
          break;
        }
        else if (strcmp(argv[i] + 2, "no-ext-ops") == 0) {
          args->no_ext_ops = TRUE;
          break;
        }
        else if (strcmp(argv[i] + 2, "no-optimize") == 0) {
          args->no_optimize = TRUE;
          break;
        }
        else if (strcmp(argv[i] + 2, "line-size") == 0) {
          mrb_bool line_size_bounds;
          mrb_int line_size;
          if (argv[i][2] == '\0' && argv[i+1]) {
            i++;
            line_size_bounds = mrb_read_int(argv[i], NULL, NULL, &line_size);
          }
          else {
            line_size_bounds = mrb_read_int(argv[i] + 2, NULL, NULL, &line_size);
          }
          if (!line_size_bounds || line_size < 1 || line_size > 255) {
            fprintf(stderr, "%s: line size out of bounds. (%d)\n", args->prog, line_size);
            return -1;
          }
          args->line_size = (uint8_t)line_size;
          break;
        }
        return -1;
      default:
        return i;
      }
    }
    else {
      break;
    }
  }
  return i;
}

static int
partial_hook(struct mrb_parser_state *p)
{
  mrbc_context *c = p->cxt;
  struct mrbc_args *args = (struct mrbc_args*)c->partial_data;
  const char *fn;

  if (p->f) fclose(p->f);
  if (args->idx >= args->argc) {
    p->f = NULL;
    return -1;
  }
  fn = args->argv[args->idx++];
  p->f = fopen(fn, "rb");
  if (p->f == NULL) {
    fprintf(stderr, "%s: cannot open program file. (%s)\n", args->prog, fn);
    return -1;
  }
  mrb_parser_set_filename(p, fn);
  return 0;
}

static mrb_value
load_file(mrb_state *mrb, struct mrbc_args *args)
{
  mrbc_context *c;
  mrb_value result;
  char *input = args->argv[args->idx];
  FILE *infile;
  mrb_bool need_close = FALSE;

  c = mrbc_context_new(mrb);
  if (args->verbose)
    c->dump_result = TRUE;
  c->no_exec = TRUE;
  c->no_ext_ops = args->no_ext_ops;
  c->no_optimize = args->no_optimize;
  if (input[0] == '-' && input[1] == '\0') {
    infile = stdin;
  }
  else {
    need_close = TRUE;
    if ((infile = fopen(input, "rb")) == NULL) {
      fprintf(stderr, "%s: cannot open program file. (%s)\n", args->prog, input);
      return mrb_nil_value();
    }
  }
  mrbc_filename(mrb, c, input);
  args->idx++;
  if (args->idx < args->argc) {
    need_close = FALSE;
    mrbc_partial_hook(mrb, c, partial_hook, (void*)args);
  }

  result = mrb_load_file_cxt(mrb, infile, c);
  if (need_close) fclose(infile);
  mrbc_context_free(mrb, c);
  if (mrb_undef_p(result)) {
    return mrb_nil_value();
  }
  return result;
}

static int
dump_file(mrb_state *mrb, FILE *wfp, const char *outfile, struct RProc *proc, struct mrbc_args *args)
{
  int n = MRB_DUMP_OK;
  const mrb_irep *irep = proc->body.irep;
  const char *file_ext = strrchr(outfile, '.');

  if (args->remove_lv) {
    mrb_irep_remove_lv(mrb, (mrb_irep*)irep);
  }
  if (args->initname) {
    if (file_ext != NULL && file_ext == C_HEAD_EXT) {
      n = mrb_dump_irep_cheader(mrb, irep, args->flags, wfp, args->initname);
    }
    else if (args->flags & MRB_DUMP_STRUCT) {
      n = mrb_dump_irep_cstruct(mrb, irep, args->flags, wfp, args->initname);
    }
    else {
      n = mrb_dump_irep_cvar(mrb, irep, args->flags, wfp, args->initname, args->line_size);
    }
    if (n == MRB_DUMP_INVALID_ARGUMENT) {
      fprintf(stderr, "%s: invalid C language symbol name\n", args->initname);
    }
  }
  else {
    if (args->flags & MRB_DUMP_STATIC) {
      fprintf(stderr, "%s: -s option requires -B<symbol>\n", args->prog);
      return MRB_DUMP_INVALID_ARGUMENT;
    }
    else {
      n = mrb_dump_irep_binary(mrb, irep, args->flags, wfp);
    }
  }
  if (n != MRB_DUMP_OK) {
    fprintf(stderr, "%s: error in mrb dump (%s) %d\n", args->prog, outfile, n);
  }
  return n;
}

int
main(int argc, char **argv)
{
  mrb_state *mrb = mrb_open_core(NULL, NULL);
  int n, result;
  struct mrbc_args args;
  FILE *wfp;
  mrb_value load;
  const char* outfilename;

  if (mrb == NULL) {
    fputs("Invalid mrb_state, exiting mrbc\n", stderr);
    return EXIT_FAILURE;
  }

  args.line_size = 16;
  n = parse_args(mrb, argc, argv, &args);
  if (n < 0) {
    mrb_close(mrb);
    usage(argv[0]);
    return EXIT_FAILURE;
  }
  if (n == argc) {
    fprintf(stderr, "%s: no program file given\n", args.prog);
    return EXIT_FAILURE;
  }
  if (args.outfile == NULL && !args.check_syntax) {
    if (n + 1 == argc) {
      outfilename = get_outfilename(mrb, args.outfile ? args.outfile, argv[n], args.initname ? C_EXT : RITEBIN_EXT);
    }
    else {
      fprintf(stderr, "%s: output file should be specified to compile multiple files\n", args.prog);
      return EXIT_FAILURE;
    }
  }

  args.idx = n;
  load = load_file(mrb, &args);
  if (mrb_nil_p(load)) {
    mrb_close(mrb);
    return EXIT_FAILURE;
  }
  if (args.check_syntax) {
    printf("%s:%s:Syntax OK\n", args.prog, argv[n]);
    mrb_close(mrb);
    return EXIT_SUCCESS;
  }

  /* bytecode/C file dump */
  if (args.outfile) {
    if (strcmp("-", args.outfile) == 0) {
      wfp = stdout;
    }
    else if ((wfp = fopen(outfilename, "wb")) == NULL) {
      fprintf(stderr, "%s: cannot open output file:(%s)\n", args.prog, outfilename);
      return EXIT_FAILURE;
    }
  }
  else {
    fputs("Output file is required\n", stderr);
    return EXIT_FAILURE;
  }
  result = dump_file(mrb, wfp, outfilename, mrb_proc_ptr(load), &args);
  fclose(wfp);
  mrb_free(mrb, (void*)outfilename);
  if (result != MRB_DUMP_OK) {
    mrb_close(mrb);
    return EXIT_FAILURE;
  }

  /* C header file dump */
  if (args.flags & MRB_DUMP_HEADER) {
    outfilename = get_outfilename(mrb, args.outfile, C_HEAD_EXT);

    if (strcmp("-", args.outfile) == 0) {
      wfp = stdout;
    }
    else if ((wfp = fopen(outfilename, "wb")) == NULL) {
      fprintf(stderr, "%s: cannot open output file:(%s)\n", args.prog, outfilename);
      return EXIT_FAILURE;
    }
    else {
      fputs("Output file is required\n", stderr);
      return EXIT_FAILURE;
    }
    result = dump_file(mrb, wfp, outfilename, mrb_proc_ptr(load), &args);
    fclose(wfp);
    mrb_free(mrb, (void*)outfilename);
    if (result != MRB_DUMP_OK) {
      mrb_close(mrb);
      return EXIT_FAILURE;
    }
  }
  mrb_close(mrb);
  return EXIT_SUCCESS;
}

void
mrb_init_mrblib(mrb_state *mrb)
{
}

#ifndef MRB_NO_GEMS
void
mrb_init_mrbgems(mrb_state *mrb)
{
}
#endif
