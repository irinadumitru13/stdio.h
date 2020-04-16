#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "so_stdio.h"

#define BUFSIZE 4096
#define READ 10
#define WRITE 20

struct _so_file {
	HANDLE handle;
	int last_op;
	int f_error;
	DWORD buf_size;
	DWORD buf_pos;
	long curr_pos;
	unsigned char *buffer;
};

FUNC_DECL_PREFIX SO_FILE *so_fopen(const char *pathname, const char *mode)
{
	HANDLE handle = INVALID_HANDLE_VALUE;
	SO_FILE *file;

	if (strcmp(mode, "r") == 0)
		handle = CreateFile(
			pathname,
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL);
	if (strcmp(mode, "r+") == 0)
		handle = CreateFile(
			pathname,
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL);
	if (strcmp(mode, "w") == 0) {
		handle = CreateFile(
			pathname,
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL,
			TRUNCATE_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL);

		if (handle == INVALID_HANDLE_VALUE)
			handle = CreateFile(
				pathname,
				GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				NULL,
				OPEN_ALWAYS,
				FILE_ATTRIBUTE_NORMAL,
				NULL);
	}
	if (strcmp(mode, "w+") == 0) {
		handle = CreateFile(
			pathname,
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL,
			TRUNCATE_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL);

		if (handle == INVALID_HANDLE_VALUE)
			handle = CreateFile(
				pathname,
				GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				NULL,
				OPEN_ALWAYS,
				FILE_ATTRIBUTE_NORMAL,
				NULL);
	}
	if (strcmp(mode, "a") == 0)
		handle = CreateFile(
			pathname,
			FILE_APPEND_DATA,
			FILE_SHARE_READ,
			NULL,
			OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			NULL);
	if (strcmp(mode, "a+") == 0)
		handle = CreateFile(
			pathname,
			FILE_APPEND_DATA | GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL,
			OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			NULL);

	if (handle == INVALID_HANDLE_VALUE)
		return NULL;

	file = malloc(sizeof(SO_FILE));
	if (file == NULL)
		return NULL;

	file->handle = handle;

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

FUNC_DECL_PREFIX int so_fclose(SO_FILE *stream)
{
	int res_c, res_f;

	res_f = so_fflush(stream);
	free(stream->buffer);
	res_c = CloseHandle(stream->handle);
	free(stream);

	if (res_f != SO_EOF && res_c != 0)
		return 0;

	return SO_EOF;
}

FUNC_DECL_PREFIX HANDLE so_fileno(SO_FILE *stream)
{
	return stream->handle;
}

FUNC_DECL_PREFIX int so_fflush(SO_FILE *stream)
{
	DWORD done, rem, rc;
	BOOL written;

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
		written = WriteFile(
			stream->handle,
			stream->buffer + done,
			rem,
			&rc,
			NULL);

		if (written == 0) {
			stream->f_error = SO_EOF;
			return SO_EOF;
		}

		if (rc == 0)
			break;

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

FUNC_DECL_PREFIX int so_fseek(SO_FILE *stream, long offset, int whence)
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

	res = SetFilePointer(
		stream->handle,
		offset,
		NULL,
		whence);
	if (res < 0) {
		stream->f_error = SO_EOF;
		return SO_EOF;
	}

	stream->curr_pos = res;

	return 0;
}

FUNC_DECL_PREFIX long so_ftell(SO_FILE *stream)
{
	return stream->curr_pos;
}

FUNC_DECL_PREFIX int so_fgetc(SO_FILE *stream)
{
	DWORD r_B;
	int res, c;
	BOOL read;

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
		read = ReadFile(
			stream->handle,
			stream->buffer,
			BUFSIZE,
			&r_B,
			NULL);
		if (read == 0) {
			stream->f_error = SO_EOF;
			return SO_EOF;
		}

		stream->buf_size = r_B;
	} else if (stream->buf_size == stream->buf_pos) {
		read = ReadFile(
			stream->handle,
			stream->buffer + stream->buf_size,
			BUFSIZE - stream->buf_size,
			&r_B,
			NULL);
		if (read == 0) {
			stream->f_error = SO_EOF;
			return SO_EOF;
		}

		stream->buf_size += r_B;
	}

	stream->last_op = READ;
	stream->curr_pos += 1;

	/*
	 * Daca c este '\0', se verifica daca nu cumva s-a ajuns
	 * la finalul fisierului
	 */
	c = (int) stream->buffer[stream->buf_pos++];
	if (c == 0 && (int) stream->buffer[stream->buf_pos] == -1) {
		stream->f_error = SO_EOF;
		return SO_EOF;
	}

	return c;
}

FUNC_DECL_PREFIX int so_fputc(int c, SO_FILE *stream)
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

FUNC_DECL_PREFIX size_t so_fread(void *ptr, size_t size,
	size_t nmemb, SO_FILE *stream)
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

FUNC_DECL_PREFIX size_t so_fwrite(const void *ptr, size_t size,
	size_t nmemb, SO_FILE *stream)
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

FUNC_DECL_PREFIX int so_feof(SO_FILE *stream)
{
	long pos1, pos2;

	pos2 = SetFilePointer(
		stream->handle,
		0,
		NULL,
		FILE_END);
	pos1 = SetFilePointer(
		stream->handle,
		stream->curr_pos,
		NULL,
		FILE_BEGIN);

	if (pos1 == pos2)
		return 0;

	return SO_EOF;
}

FUNC_DECL_PREFIX int so_ferror(SO_FILE *stream)
{
	return stream->f_error;
}

FUNC_DECL_PREFIX SO_FILE *so_popen(const char *command, const char *type)
{
	return NULL;
}

FUNC_DECL_PREFIX int so_pclose(SO_FILE *stream)
{
	return SO_EOF;
}
