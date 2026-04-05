/*
 * nfscopy - copy files to/from an NFS server using libnfs
 *
 * Usage:
 *   nfscopy <source> <destination>
 *
 *   Either source or destination (or both) must be an NFS URL:
 *     nfs://<server>/<export>/<path>
 *
 * Examples:
 *   nfscopy /local/file.txt nfs://192.168.1.1/mnt/data/file.txt
 *   nfscopy nfs://192.168.1.1/mnt/data/file.txt /local/file.txt
 *   nfscopy nfs://server/export/src.txt nfs://server/export/dst.txt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <nfsc/libnfs.h>

#define CHUNK_SIZE      (1024 * 1024)   /* 1 MiB read/write chunks */
#define PROGRESS_EVERY  (10 * 1024 * 1024)  /* print progress every 10 MiB */

static int is_nfs_url(const char *path)
{
    return strncmp(path, "nfs://", 6) == 0;
}

static void print_progress(off_t bytes)
{
    if (bytes < 1024 * 1024)
        fprintf(stderr, "\r  %lld B", (long long)bytes);
    else if (bytes < 1024 * 1024 * 1024)
        fprintf(stderr, "\r  %.1f MiB", (double)bytes / (1024.0 * 1024.0));
    else
        fprintf(stderr, "\r  %.2f GiB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    fflush(stderr);
}

/* ------------------------------------------------------------------ */
/* Local → NFS                                                          */
/* ------------------------------------------------------------------ */
static int copy_local_to_nfs(const char *src, struct nfs_context *nfs,
                              const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in) {
        fprintf(stderr, "Error: cannot open local source '%s': %s\n",
                src, strerror(errno));
        return -1;
    }

    struct nfsfh *out = NULL;
    if (nfs_creat(nfs, dst, 0644, &out) != 0) {
        fprintf(stderr, "Error: cannot create NFS file '%s': %s\n",
                dst, nfs_get_error(nfs));
        fclose(in);
        return -1;
    }

    char *buf = malloc(CHUNK_SIZE);
    if (!buf) {
        fprintf(stderr, "Error: out of memory\n");
        nfs_close(nfs, out);
        fclose(in);
        return -1;
    }

    size_t n;
    off_t total = 0;
    off_t next_report = PROGRESS_EVERY;
    int err = 0;

    while ((n = fread(buf, 1, CHUNK_SIZE, in)) > 0) {
        int written = nfs_write(nfs, out, buf, (uint64_t)n);
        if (written < 0) {
            fprintf(stderr, "\nError: NFS write failed: %s\n",
                    nfs_get_error(nfs));
            err = -1;
            break;
        }
        total += written;
        if (total >= next_report) {
            print_progress(total);
            next_report += PROGRESS_EVERY;
        }
    }

    if (!err && ferror(in)) {
        fprintf(stderr, "\nError: reading local source '%s': %s\n",
                src, strerror(errno));
        err = -1;
    }

    if (!err && total > 0) {
        print_progress(total);
        fprintf(stderr, "\n");
    }

    free(buf);
    nfs_close(nfs, out);
    fclose(in);
    return err;
}

/* ------------------------------------------------------------------ */
/* NFS → Local                                                          */
/* ------------------------------------------------------------------ */
static int copy_nfs_to_local(struct nfs_context *nfs, const char *src,
                              const char *dst)
{
    struct nfsfh *in = NULL;
    if (nfs_open(nfs, src, O_RDONLY, &in) != 0) {
        fprintf(stderr, "Error: cannot open NFS file '%s': %s\n",
                src, nfs_get_error(nfs));
        return -1;
    }

    FILE *out = fopen(dst, "wb");
    if (!out) {
        fprintf(stderr, "Error: cannot create local file '%s': %s\n",
                dst, strerror(errno));
        nfs_close(nfs, in);
        return -1;
    }

    char *buf = malloc(CHUNK_SIZE);
    if (!buf) {
        fprintf(stderr, "Error: out of memory\n");
        nfs_close(nfs, in);
        fclose(out);
        return -1;
    }

    int n;
    off_t total = 0;
    off_t next_report = PROGRESS_EVERY;
    int err = 0;

    while ((n = nfs_read(nfs, in, buf, CHUNK_SIZE)) > 0) {
        if (fwrite(buf, 1, (size_t)n, out) != (size_t)n) {
            fprintf(stderr, "\nError: writing local file '%s': %s\n",
                    dst, strerror(errno));
            err = -1;
            break;
        }
        total += n;
        if (total >= next_report) {
            print_progress(total);
            next_report += PROGRESS_EVERY;
        }
    }

    if (!err && n < 0) {
        fprintf(stderr, "\nError: NFS read failed: %s\n",
                nfs_get_error(nfs));
        err = -1;
    }

    if (!err && total > 0) {
        print_progress(total);
        fprintf(stderr, "\n");
    }

    /* Remove partial destination file on error */
    if (err) {
        fclose(out);
        unlink(dst);
    } else {
        fclose(out);
    }

    free(buf);
    nfs_close(nfs, in);
    return err;
}

/* ------------------------------------------------------------------ */
/* NFS → NFS  (same server + export)                                    */
/* ------------------------------------------------------------------ */
static int copy_nfs_to_nfs(struct nfs_context *nfs, const char *src,
                            const char *dst)
{
    struct nfsfh *in = NULL;
    if (nfs_open(nfs, src, O_RDONLY, &in) != 0) {
        fprintf(stderr, "Error: cannot open NFS source '%s': %s\n",
                src, nfs_get_error(nfs));
        return -1;
    }

    struct nfsfh *out = NULL;
    if (nfs_creat(nfs, dst, 0644, &out) != 0) {
        fprintf(stderr, "Error: cannot create NFS destination '%s': %s\n",
                dst, nfs_get_error(nfs));
        nfs_close(nfs, in);
        return -1;
    }

    char *buf = malloc(CHUNK_SIZE);
    if (!buf) {
        fprintf(stderr, "Error: out of memory\n");
        nfs_close(nfs, in);
        nfs_close(nfs, out);
        return -1;
    }

    int n;
    off_t total = 0;
    off_t next_report = PROGRESS_EVERY;
    int err = 0;

    while ((n = nfs_read(nfs, in, buf, CHUNK_SIZE)) > 0) {
        int written = nfs_write(nfs, out, buf, (uint64_t)n);
        if (written < 0) {
            fprintf(stderr, "\nError: NFS write failed: %s\n",
                    nfs_get_error(nfs));
            err = -1;
            break;
        }
        total += written;
        if (total >= next_report) {
            print_progress(total);
            next_report += PROGRESS_EVERY;
        }
    }

    if (!err && n < 0) {
        fprintf(stderr, "\nError: NFS read failed: %s\n",
                nfs_get_error(nfs));
        err = -1;
    }

    if (!err && total > 0) {
        print_progress(total);
        fprintf(stderr, "\n");
    }

    free(buf);
    nfs_close(nfs, in);
    nfs_close(nfs, out);
    return err;
}

/* ------------------------------------------------------------------ */
/* Mount helper — prints a clean error and returns the context or NULL  */
/* ------------------------------------------------------------------ */
static int do_mount(struct nfs_context *nfs, const char *server,
                    const char *export_path)
{
    fprintf(stderr, "Mounting %s:%s ...\n", server, export_path);
    int ret = nfs_mount(nfs, server, export_path);
    if (ret != 0) {
        fprintf(stderr, "Error: NFS mount failed for %s:%s — %s\n",
                server, export_path, nfs_get_error(nfs));
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/* Usage                                                                */
/* ------------------------------------------------------------------ */
static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <source> <destination>\n"
        "\n"
        "  At least one path must be an NFS URL:\n"
        "    nfs://<server>/<export>/<path>\n"
        "\n"
        "Examples:\n"
        "  %s /local/file.txt nfs://192.168.1.1/mnt/data/file.txt\n"
        "  %s nfs://192.168.1.1/mnt/data/file.txt /local/copy.txt\n"
        "  %s nfs://server/export/src.txt nfs://server/export/dst.txt\n",
        prog, prog, prog, prog);
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    if (argc != 3) {
        usage(argv[0]);
        return 1;
    }

    const char *src = argv[1];
    const char *dst = argv[2];
    int src_is_nfs  = is_nfs_url(src);
    int dst_is_nfs  = is_nfs_url(dst);

    if (!src_is_nfs && !dst_is_nfs) {
        fprintf(stderr,
            "Error: at least one of source or destination must be an NFS URL "
            "(nfs://...)\n");
        usage(argv[0]);
        return 1;
    }

    struct nfs_context *nfs = nfs_init_context();
    if (!nfs) {
        fprintf(stderr, "Error: failed to initialise NFS context\n");
        return 1;
    }

    int ret = 0;

    /* ---- NFS → NFS ---- */
    if (src_is_nfs && dst_is_nfs) {
        struct nfs_url *src_url = nfs_parse_url_full(nfs, src);
        if (!src_url) {
            fprintf(stderr, "Error: bad source URL '%s': %s\n",
                    src, nfs_get_error(nfs));
            nfs_destroy_context(nfs);
            return 1;
        }
        struct nfs_url *dst_url = nfs_parse_url_full(nfs, dst);
        if (!dst_url) {
            fprintf(stderr, "Error: bad destination URL '%s': %s\n",
                    dst, nfs_get_error(nfs));
            nfs_destroy_url(src_url);
            nfs_destroy_context(nfs);
            return 1;
        }

        if (strcmp(src_url->server, dst_url->server) != 0 ||
            strcmp(src_url->path,   dst_url->path)   != 0) {
            fprintf(stderr,
                "Error: NFS-to-NFS copy requires the same server and export.\n"
                "  source : nfs://%s%s\n"
                "  dest   : nfs://%s%s\n",
                src_url->server, src_url->path,
                dst_url->server, dst_url->path);
            nfs_destroy_url(src_url);
            nfs_destroy_url(dst_url);
            nfs_destroy_context(nfs);
            return 1;
        }

        if (do_mount(nfs, src_url->server, src_url->path) == 0)
            ret = copy_nfs_to_nfs(nfs, src_url->file, dst_url->file);
        else
            ret = -1;

        nfs_destroy_url(src_url);
        nfs_destroy_url(dst_url);

    /* ---- NFS → Local ---- */
    } else if (src_is_nfs) {
        struct nfs_url *url = nfs_parse_url_full(nfs, src);
        if (!url) {
            fprintf(stderr, "Error: bad source URL '%s': %s\n",
                    src, nfs_get_error(nfs));
            nfs_destroy_context(nfs);
            return 1;
        }

        if (do_mount(nfs, url->server, url->path) == 0)
            ret = copy_nfs_to_local(nfs, url->file, dst);
        else
            ret = -1;

        nfs_destroy_url(url);

    /* ---- Local → NFS ---- */
    } else {
        struct nfs_url *url = nfs_parse_url_full(nfs, dst);
        if (!url) {
            fprintf(stderr, "Error: bad destination URL '%s': %s\n",
                    dst, nfs_get_error(nfs));
            nfs_destroy_context(nfs);
            return 1;
        }

        if (do_mount(nfs, url->server, url->path) == 0)
            ret = copy_local_to_nfs(src, nfs, url->file);
        else
            ret = -1;

        nfs_destroy_url(url);
    }

    nfs_destroy_context(nfs);
    return ret == 0 ? 0 : 1;
}
