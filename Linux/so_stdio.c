#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "so_stdio.h"

#define BUFSIZE 4096
#define READ 10
#define WRITE 20

struct _so_file {
	int fd;
	int last_op;
	int f_error;
	ssize_t buf_size;
	ssize_t buf_pos;
	long curr_pos;
	unsigned char *buffer;
};

SO_FILE *so_fopen(const char *pathname, const char *mode)
{
	int file_desc = SO_EOF;
	SO_FILE *file;

	if (strcmp(mode, "r") == 0)
		file_desc = open(pathname, O_RDONLY);
	if (strcmp(mode, "r+") == 0)
		file_desc = open(pathname, O_RDWR);
	if (strcmp(mode, "w") == 0)
		file_desc = open(pathname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (strcmp(mode, "w+") == 0)
		file_desc = open(pathname, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (strcmp(mode, "a") == 0)
		file_desc = open(pathname, O_WRONLY | O_APPEND | O_CREAT, 0644);
	if (strcmp(mode, "a+") == 0)
		file_desc = open(pathname, O_RDWR | O_APPEND | O_CREAT, 0644);

	if (file_desc == SO_EOF)
		return NULL;

	file = malloc(sizeof(SO_FILE));
	if (file == NULL)
		return NULL;

	file->fd = file_desc;

	file->buffer = malloc(BUFSIZE * sizeof(unsigned char));
	if (file->buffer == NULL) {
		free(file);
		return NULL;
	}
	memset(file->buffer, 0, BUFSIZE);

	file->last_op = SO_EOF;
	file->buf_size = 0;
	file->buf_pos = 0;
	file->curr_pos = 0;
	file->f_error = 0;

	return file;
}

int so_fclose(SO_FILE *stream)
{
	int res_c, res_f;

	res_f = so_fflush(stream);
	free(stream->buffer);
	res_c = close(stream->fd);
	free(stream);

	if (res_f != SO_EOF && res_c != SO_EOF)
		return res_c;

	return SO_EOF;
}

int so_fileno(SO_FILE *stream)
{
	return stream->fd;
}

int so_fflush(SO_FILE *stream)
{
	size_t done, rem, rc;

	/*
	 * Daca ultima operatie este de read, se invalideaza buffer-ul
	 */
	if (stream->last_op == READ) {
		memset(stream->buffer, 0, stream->buf_size);
		stream->buf_size = 0;
		stream->buf_pos = 0;
	}

	/*
	 * Se scrie continutul buffer-ului in fisier
	 */
	rem = stream->buf_size;
	done = 0;

	while (done < stream->buf_size) {
		rc = write(stream->fd, stream->buffer + done, rem);

		if (rc == 0)
			break;

		if (rc == SO_EOF) {
			stream->f_error = SO_EOF;
			return SO_EOF;
		}

		rem -= rc;
		done += rc;
	}

	/*
	 * Sterge continutul buffer-ului
	 */
	memset(stream->buffer, 0, stream->buf_size);
	stream->buf_size = 0;
	stream->buf_pos = 0;

	return 0;
}

int so_fseek(SO_FILE *stream, long offset, int whence)
{
	int res;

	/*
	 * Daca ultima operatie a fost de READ, se invalideaza buffer-ul
	 */
	if (stream->last_op == READ) {
		memset(stream->buffer, 0, BUFSIZE);
		stream->buf_pos = 0;
		stream->buf_size = 0;
		stream->last_op = SO_EOF;
	}

	/*
	 * Daca ultima operatie a fost de WRITE, se da flush pe buffer
	 */
	if (stream->last_op == WRITE) {
		res = so_fflush(stream);
		if (res == SO_EOF) {
			stream->f_error = SO_EOF;
			return SO_EOF;
		}
	}

	res = lseek(stream->fd, offset, whence);
	if (res == SO_EOF) {
		stream->f_error = SO_EOF;
		return SO_EOF;
	}

	stream->curr_pos = res;

	return 0;
}

long so_ftell(SO_FILE *stream)
{
	return stream->curr_pos;
}

int so_fgetc(SO_FILE *stream)
{
	ssize_t r_B;
	int res, c;

	/*
	 * Daca ultima operatie a fost de WRITE, se da flush la buffer
	 * Se face fseek la pozitia la care ramasesem inainte de write
	 */
	if (stream->last_op == WRITE) {
		res = so_fflush(stream);
		if (res == SO_EOF) {
			stream->f_error = SO_EOF;
			return SO_EOF;
		}

		res = so_fseek(stream, stream->curr_pos, SEEK_SET);
		if (res == SO_EOF) {
			stream->f_error = SO_EOF;
			return SO_EOF;
		}
	}

	/*
	 * Daca buffer-ul este plin, se invalideaza continutul
	 */
	if (stream->buf_size == BUFSIZE && stream->buf_pos == BUFSIZE) {
		memset(stream->buffer, 0, BUFSIZE);
		stream->buf_size = 0;
		stream->buf_pos = 0;
	}

	/*
	 * Daca buffer-ul este gol, se baga date in buffer
	 * Daca acesta nu este gol, dar nu si-a atins limita,
	 * se vor baga date in continuarea celor existente
	 */
	if (stream->buf_size == 0) {
		r_B = read(stream->fd, stream->buffer, BUFSIZE);
		if (r_B == SO_EOF) {
			stream->f_error = SO_EOF;
			return SO_EOF;
		}

		stream->buf_size = r_B;
	} else if (stream->buf_size == stream->buf_pos) {
		r_B = read(stream->fd, stream->buffer + stream->buf_size,
					BUFSIZE - stream->buf_size);
		if (r_B == SO_EOF) {
			stream->f_error = SO_EOF;
			return SO_EOF;
		}

		stream->buf_size += r_B;
	}

	stream->last_op = READ;
	stream->curr_pos += 1;
	c = (int) stream->buffer[stream->buf_pos++];

	/*
	 * Daca c este '\0', se verifica daca nu cumva s-a ajuns
	 * la finalul fisierului
	 */
	if (c == 0 && (int) stream->buffer[stream->buf_pos] == -1) {
		stream->f_error = SO_EOF;
		return SO_EOF;
	}

	return c;
}

int so_fputc(int c, SO_FILE *stream)
{
	int res;

	/*
	 * Daca ultima operatie a fost de READ, se face fseek la final
	 */
	if (stream->last_op == READ) {
		res = so_fseek(stream, 0, SEEK_END);
		if (res == SO_EOF) {
			stream->f_error = SO_EOF;
			return SO_EOF;
		}
	}

	/*
	 * Daca buffer-ul este plin, se face flush
	 */
	if (stream->buf_size == BUFSIZE) {
		res = so_fflush(stream);
		if (res == SO_EOF) {
			stream->f_error = SO_EOF;
			return SO_EOF;
		}
	}

	stream->buffer[stream->buf_size] = (unsigned char) c;
	stream->buf_size += 1;
	stream->curr_pos += 1;
	stream->last_op = WRITE;

	return c;
}

size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	size_t bytes_read = size * nmemb, i;
	int c, got_to_stop = 0;

	for (i = 0; i < bytes_read; i++) {
		if (got_to_stop == 1) {
			i--;
			break;
		}

		/*
		 * Se verifica daca s-a ajuns la finalul fisierului
		 * Daca da, se mai intra o data in for, pt a scrie
		 * caracterul in ptr
		 */
		if (so_feof(stream) == 0)
			got_to_stop = 1;

		c = so_fgetc(stream);
		if (c == SO_EOF) {
			stream->f_error = SO_EOF;
			return 0;
		}

		*((unsigned char *) ptr + i) = c;
	}

	return i / size;
}

size_t so_fwrite(const void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	size_t bytes_write = size * nmemb, i;
	int c, res;
	unsigned char *written = (unsigned char *) ptr;

	for (i = 0; i < bytes_write; i++) {
		c = (int) *(written + i);

		res = so_fputc(c, stream);
		if (res == SO_EOF) {
			stream->f_error = SO_EOF;
			return 0;
		}
	}

	return nmemb;
}

int so_feof(SO_FILE *stream)
{
	long pos1, pos2;

	pos2 = lseek(stream->fd, 0, SEEK_END);
	pos1 = lseek(stream->fd, stream->curr_pos, SEEK_SET);

	if (pos1 == pos2)
		return 0;

	return SO_EOF;
}

int so_ferror(SO_FILE *stream)
{
	return stream->f_error;
}

SO_FILE *so_popen(const char *command, const char *type)
{
	return NULL;
}

int so_pclose(SO_FILE *stream)
{
	return SO_EOF;
}
