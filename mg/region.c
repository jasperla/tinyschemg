/*	$OpenBSD: region.c,v 1.30 2012/04/11 17:51:10 lum Exp $	*/

/* This file is in the public domain. */

/*
 *		Region based commands.
 * The routines in this file deal with the region, that magic space between
 * "." and mark.  Some functions are commands.  Some functions are just for
 * internal use.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

#include "def.h"

#define TIMEOUT 10000

static char leftover[BUFSIZ];

static	int	getregion(struct region *);
static	int	iomux(int);
static	int	pipeio(const char *);
static	int	preadin(int, struct buffer *);
static	void	pwriteout(int, char **, int *);
static	int	setsize(struct region *, RSIZE);

/*
 * Kill the region.  Ask "getregion" to figure out the bounds of the region.
 * Move "." to the start, and kill the characters. Mark is cleared afterwards.
 */
/* ARGSUSED */
int
killregion(int f, int n)
{
	int	s;
	struct region	region;

	if ((s = getregion(&region)) != TRUE)
		return (s);
	/* This is a kill-type command, so do magic kill buffer stuff. */
	if ((lastflag & CFKILL) == 0)
		kdelete();
	thisflag |= CFKILL;
	curwp->w_dotp = region.r_linep;
	curwp->w_doto = region.r_offset;
	curwp->w_dotline = region.r_lineno;
	s = ldelete(region.r_size, KFORW | KREG);
	clearmark(FFARG, 0);

	return (s);
}

/*
 * Copy all of the characters in the region to the kill buffer,
 * clearing the mark afterwards.
 * This is a bit like a kill region followed by a yank.
 */
/* ARGSUSED */
int
copyregion(int f, int n)
{
	struct line	*linep;
	struct region	 region;
	int	 loffs;
	int	 s;

	if ((s = getregion(&region)) != TRUE)
		return (s);

	/* kill type command */
	if ((lastflag & CFKILL) == 0)
		kdelete();
	thisflag |= CFKILL;

	/* current line */
	linep = region.r_linep;

	/* current offset */
	loffs = region.r_offset;

	while (region.r_size--) {
		if (loffs == llength(linep)) {	/* End of line.		 */
			if ((s = kinsert('\n', KFORW)) != TRUE)
				return (s);
			linep = lforw(linep);
			loffs = 0;
		} else {			/* Middle of line.	 */
			if ((s = kinsert(lgetc(linep, loffs), KFORW)) != TRUE)
				return (s);
			++loffs;
		}
	}
	clearmark(FFARG, 0);

	return (TRUE);
}

/*
 * Lower case region.  Zap all of the upper case characters in the region to
 * lower case. Use the region code to set the limits. Scan the buffer, doing
 * the changes. Call "lchange" to ensure that redisplay is done in all
 * buffers.
 */
/* ARGSUSED */
int
lowerregion(int f, int n)
{
	struct line	*linep;
	struct region	 region;
	int	 loffs, c, s;

	if ((s = checkdirty(curbp)) != TRUE)
		return (s);
	if (curbp->b_flag & BFREADONLY) {
		ewprintf("Buffer is read-only");
		return (FALSE);
	}

	if ((s = getregion(&region)) != TRUE)
		return (s);

	undo_add_change(region.r_linep, region.r_offset, region.r_size);

	lchange(WFFULL);
	linep = region.r_linep;
	loffs = region.r_offset;
	while (region.r_size--) {
		if (loffs == llength(linep)) {
			linep = lforw(linep);
			loffs = 0;
		} else {
			c = lgetc(linep, loffs);
			if (ISUPPER(c) != FALSE)
				lputc(linep, loffs, TOLOWER(c));
			++loffs;
		}
	}
	return (TRUE);
}

/*
 * Upper case region.  Zap all of the lower case characters in the region to
 * upper case.  Use the region code to set the limits.  Scan the buffer,
 * doing the changes.  Call "lchange" to ensure that redisplay is done in all
 * buffers.
 */
/* ARGSUSED */
int
upperregion(int f, int n)
{
	struct line	 *linep;
	struct region	  region;
	int	  loffs, c, s;

	if ((s = checkdirty(curbp)) != TRUE)
		return (s);
	if (curbp->b_flag & BFREADONLY) {
		ewprintf("Buffer is read-only");
		return (FALSE);
	}
	if ((s = getregion(&region)) != TRUE)
		return (s);

	undo_add_change(region.r_linep, region.r_offset, region.r_size);

	lchange(WFFULL);
	linep = region.r_linep;
	loffs = region.r_offset;
	while (region.r_size--) {
		if (loffs == llength(linep)) {
			linep = lforw(linep);
			loffs = 0;
		} else {
			c = lgetc(linep, loffs);
			if (ISLOWER(c) != FALSE)
				lputc(linep, loffs, TOUPPER(c));
			++loffs;
		}
	}
	return (TRUE);
}

/*
 * This routine figures out the bound of the region in the current window,
 * and stores the results into the fields of the REGION structure. Dot and
 * mark are usually close together, but I don't know the order, so I scan
 * outward from dot, in both directions, looking for mark. The size is kept
 * in a long. At the end, after the size is figured out, it is assigned to
 * the size field of the region structure. If this assignment loses any bits,
 * then we print an error. This is "type independent" overflow checking. All
 * of the callers of this routine should be ready to get an ABORT status,
 * because I might add a "if regions is big, ask before clobbering" flag.
 */
static int
getregion(struct region *rp)
{
	struct line	*flp, *blp;
	long	 fsize, bsize;

	if (curwp->w_markp == NULL) {
		ewprintf("No mark set in this window");
		return (FALSE);
	}

	/* "r_size" always ok */
	if (curwp->w_dotp == curwp->w_markp) {
		rp->r_linep = curwp->w_dotp;
		rp->r_lineno = curwp->w_dotline;
		if (curwp->w_doto < curwp->w_marko) {
			rp->r_offset = curwp->w_doto;
			rp->r_size = (RSIZE)(curwp->w_marko - curwp->w_doto);
		} else {
			rp->r_offset = curwp->w_marko;
			rp->r_size = (RSIZE)(curwp->w_doto - curwp->w_marko);
		}
		return (TRUE);
	}
	/* get region size */
	flp = blp = curwp->w_dotp;
	bsize = curwp->w_doto;
	fsize = llength(flp) - curwp->w_doto + 1;
	while (lforw(flp) != curbp->b_headp || lback(blp) != curbp->b_headp) {
		if (lforw(flp) != curbp->b_headp) {
			flp = lforw(flp);
			if (flp == curwp->w_markp) {
				rp->r_linep = curwp->w_dotp;
				rp->r_offset = curwp->w_doto;
				rp->r_lineno = curwp->w_dotline;
				return (setsize(rp,
				    (RSIZE)(fsize + curwp->w_marko)));
			}
			fsize += llength(flp) + 1;
		}
		if (lback(blp) != curbp->b_headp) {
			blp = lback(blp);
			bsize += llength(blp) + 1;
			if (blp == curwp->w_markp) {
				rp->r_linep = blp;
				rp->r_offset = curwp->w_marko;
				rp->r_lineno = curwp->w_markline;
				return (setsize(rp,
				    (RSIZE)(bsize - curwp->w_marko)));
			}
		}
	}
	ewprintf("Bug: lost mark");
	return (FALSE);
}

/*
 * Set size, and check for overflow.
 */
static int
setsize(struct region *rp, RSIZE size)
{
	rp->r_size = size;
	if (rp->r_size != size) {
		ewprintf("Region is too large");
		return (FALSE);
	}
	return (TRUE);
}

#define PREFIXLENGTH 40
static char	prefix_string[PREFIXLENGTH] = {'>', '\0'};

/*
 * Prefix the region with whatever is in prefix_string.  Leaves dot at the
 * beginning of the line after the end of the region.  If an argument is
 * given, prompts for the line prefix string.
 */
/* ARGSUSED */
int
prefixregion(int f, int n)
{
	struct line	*first, *last;
	struct region	 region;
	char	*prefix = prefix_string;
	int	 nline;
	int	 s;

	if ((s = checkdirty(curbp)) != TRUE)
		return (s);
	if (curbp->b_flag & BFREADONLY) {
		ewprintf("Buffer is read-only");
		return (FALSE);
	}
	if ((f == TRUE) && ((s = setprefix(FFRAND, 1)) != TRUE))
		return (s);

	/* get # of lines to affect */
	if ((s = getregion(&region)) != TRUE)
		return (s);
	first = region.r_linep;
	last = (first == curwp->w_dotp) ? curwp->w_markp : curwp->w_dotp;
	for (nline = 1; first != last; nline++)
		first = lforw(first);

	/* move to beginning of region */
	curwp->w_dotp = region.r_linep;
	curwp->w_doto = region.r_offset;
	curwp->w_dotline = region.r_lineno;

	/* for each line, go to beginning and insert the prefix string */
	while (nline--) {
		(void)gotobol(FFRAND, 1);
		for (prefix = prefix_string; *prefix; prefix++)
			(void)linsert(1, *prefix);
		(void)forwline(FFRAND, 1);
	}
	(void)gotobol(FFRAND, 1);
	return (TRUE);
}

/*
 * Set line prefix string. Used by prefixregion.
 */
/* ARGSUSED */
int
setprefix(int f, int n)
{
	char	buf[PREFIXLENGTH], *rep;
	int	retval;

	if (prefix_string[0] == '\0')
		rep = eread("Prefix string: ", buf, sizeof(buf),
		    EFNEW | EFCR);
	else
		rep = eread("Prefix string (default %s): ", buf, sizeof(buf),
		    EFNUL | EFNEW | EFCR, prefix_string);
	if (rep == NULL)
		return (ABORT);
	if (rep[0] != '\0') {
		(void)strlcpy(prefix_string, rep, sizeof(prefix_string));
		retval = TRUE;
	} else if (rep[0] == '\0' && prefix_string[0] != '\0') {
		/* CR -- use old one */
		retval = TRUE;
	} else
		retval = FALSE;
	return (retval);
}

int
region_get_data(struct region *reg, char *buf, int len)
{
	int	 i, off;
	struct line	*lp;

	off = reg->r_offset;
	lp = reg->r_linep;
	for (i = 0; i < len; i++) {
		if (off == llength(lp)) {
			lp = lforw(lp);
			if (lp == curbp->b_headp)
				break;
			off = 0;
			buf[i] = '\n';
		} else {
			buf[i] = lgetc(lp, off);
			off++;
		}
	}
	buf[i] = '\0';
	return (i);
}

void
region_put_data(const char *buf, int len)
{
	int i;

	for (i = 0; buf[i] != '\0' && i < len; i++) {
		if (buf[i] == '\n')
			lnewline();
		else
			linsert(1, buf[i]);
	}
}

/*
 * Mark whole buffer by first traversing to end-of-buffer
 * and then to beginning-of-buffer. Mark, dot are implicitly
 * set to eob, bob respectively during traversal.
 */
int
markbuffer(int f, int n)
{
	if (gotoeob(f,n) == FALSE)
		return (FALSE);
	if (gotobob(f,n) == FALSE)
		return (FALSE);
	return (TRUE);
}

/*
 * Pipe text from current region to external command.
 */
/*ARGSUSED */
int
piperegion(int f, int n)
{
	char *cmd, cmdbuf[NFILEN];

	/* C-u M-| is not supported yet */
	if (n > 1)
		return (ABORT);

	if (curwp->w_markp == NULL) {
		ewprintf("The mark is not set now, so there is no region");
		return (FALSE);
	}
	if ((cmd = eread("Shell command on region: ", cmdbuf, sizeof(cmdbuf),
	    EFNEW | EFCR)) == NULL || (cmd[0] == '\0'))
		return (ABORT);

	return (pipeio(cmdbuf));
}

/*
 * Create a socketpair, fork and execl cmd passed. STDIN, STDOUT
 * and STDERR of child process are redirected to socket.
 */
int
pipeio(const char* const cmd)
{
	int s[2];
	char *shellp;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, s) == -1) {
		ewprintf("socketpair error");
		return (FALSE);
	}
	switch(fork()) {
	case -1:
		ewprintf("Can't fork");
		return (FALSE);
	case 0:
		/* Child process */
		close(s[0]);
		if (dup2(s[1], STDIN_FILENO) == -1)
			_exit(1);
		if (dup2(s[1], STDOUT_FILENO) == -1)
			_exit(1);
		if (dup2(s[1], STDERR_FILENO) == -1)
			_exit(1);
		if ((shellp = getenv("SHELL")) == NULL)
			_exit(1);
		execl(shellp, "sh", "-c", cmd, (char *)NULL);
		_exit(1);
	default:
		/* Parent process */
		close(s[1]);
		return iomux(s[0]);
	}
	return (FALSE);
}

/*
 * Multiplex read, write on socket fd passed. First get the region,
 * find/create *Shell Command Output* buffer and clear it's contents.
 * Poll on the fd for both read and write readiness.
 */
int
iomux(int fd)
{
	struct region region;
	struct buffer *bp;
	struct pollfd pfd[1];
	int nfds;
	char *text, *textcopy;
	
	if (getregion(&region) != TRUE)
		return (FALSE);
	
	if ((text = malloc(region.r_size + 1)) == NULL)
		return (ABORT);
	
	region_get_data(&region, text, region.r_size);
	textcopy = text;
	fcntl(fd, F_SETFL, O_NONBLOCK);
	
	/* There is nothing to write if r_size is zero
	 * but the cmd's output should be read so shutdown 
	 * the socket for writing only.
	 */
	if (region.r_size == 0)
		shutdown(fd, SHUT_WR);
	
	bp = bfind("*Shell Command Output*", TRUE);
	bp->b_flag |= BFREADONLY;
	if (bclear(bp) != TRUE)
		return (FALSE);

	pfd[0].fd = fd;
	pfd[0].events = POLLIN | POLLOUT;
	while ((nfds = poll(pfd, 1, TIMEOUT)) != -1 ||
	    (pfd[0].revents & (POLLERR | POLLHUP | POLLNVAL))) {
		if (pfd[0].revents & POLLOUT && region.r_size > 0)
			pwriteout(fd, &textcopy, &region.r_size);	
		else if (pfd[0].revents & POLLIN)
			if (preadin(fd, bp) == FALSE)
				break;
	}
	close(fd);
	free(text);
	/* In case if last line doesn't have a '\n' add the leftover 
	 * characters to buffer.
	 */
	if (leftover[0] != '\0') {
		addline(bp, leftover);
		leftover[0] = '\0';
	}
	if (nfds == 0) {
		ewprintf("poll timed out");
		return (FALSE);
	} else if (nfds == -1) {
		ewprintf("poll error");
		return (FALSE);
	}
	return (popbuftop(bp, WNONE));
}

/*
 * Write some text from region to fd. Once done shutdown the 
 * write end.
 */
void
pwriteout(int fd, char **text, int *len)
{
	int w;

	if (((w = send(fd, *text, *len, MSG_NOSIGNAL)) == -1)) {
		switch(errno) {
		case EPIPE:
			*len = -1;
			break;
		case EAGAIN:
			return;
		}
	} else
		*len -= w;

	*text += w;
	if (*len <= 0)
		shutdown(fd, SHUT_WR);		
}

/*
 * Read some data from socket fd, break on '\n' and add
 * to buffer. If couldn't break on newline hold leftover
 * characters and append in next iteration.
 */
int
preadin(int fd, struct buffer *bp)
{
	int len;
	static int nooutput;
	char buf[BUFSIZ], *p, *q;

	if ((len = read(fd, buf, BUFSIZ - 1)) == 0) {
		if (nooutput == 0)
			addline(bp, "(Shell command succeeded with no output)");
		nooutput = 0;
		return (FALSE);
	}
	nooutput = 1;
	buf[len] = '\0';
	p = q = buf;
	if (leftover[0] != '\0' && ((q = strchr(p, '\n')) != NULL)) {
		*q++ = '\0';
		if (strlcat(leftover, p, sizeof(leftover)) >=
		    sizeof(leftover)) {
			ewprintf("line too long");
			return (FALSE);
		}
		addline(bp, leftover);
		leftover[0] = '\0';
		p = q;
	}
	while ((q = strchr(p, '\n')) != NULL) {
		*q++ = '\0';
		addline(bp, p);
		p = q;
	}
	if (strlcpy(leftover, p, sizeof(leftover)) >= sizeof(leftover)) {
		ewprintf("line too long");
		return (FALSE);
	}
	return (TRUE);
}
