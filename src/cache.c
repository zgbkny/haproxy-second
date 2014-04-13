#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>

#include <common/config.h>
#include <common/debug.h>
#include <common/memory.h>
#include <common/logging.h>


#include <proto/cache.h>
#include <proto/session.h>
#include <proto/protocols.h>
#include <proto/proto_http.h>
#include <proto/proto_tcp.h>


/*haproxy-second*/
int process_cache_file(struct session *s)
{
	logging(TRACE, "[process_cache_file]");
	char file[100] = "/home/ww/cache";
    char size[100] = "\0";
	struct http_txn *txn = &s->txn;
	struct http_msg *msg_req = &txn->req;
	struct http_msg *msg_rsp = &txn->rsp;
	logging(TRACE, "data:%s\nmeth:%d,uri:%s\n", msg_req->buf->data, txn->meth, txn->uri);
    int i = strlen(file), j = 0, max = 0, readl = 0, read_len = 0, cur_read = 0;
	if (!s->fp) {
		for ( ; txn->uri[j] != ' '; j++) {

		}
		j++;
		for ( ; txn->uri[j] != ' '; i++, j++) {
			file[i] = txn->uri[j];
		}
		file[i] = 0;
		logging(TRACE, "process_session:file:%s\n", file);

		FILE *fp = fopen(file, "rb");
		if (fp) {
			s->fp = fp;
			s->cache = 1;
			s->offset = 0;
			fseek(fp, 0L, SEEK_END);
			s->size = ftell(fp);
			sprintf(size, "%d", s->size);
            fseek(fp, 0L, SEEK_SET);
			msg_req->buf->p = msg_req->buf->data;
			msg_req->buf->i = 0;
			msg_req->buf->o = 0;
			msg_req->buf->to_forward = 0;
			msg_rsp->buf->data[0] = 0;
			memset(msg_req->buf->data, 0, sizeof(char)*msg_req->buf->size);
			memcpy(msg_rsp->buf->p, HTTP_200, strlen(HTTP_200));
			memcpy(msg_rsp->buf->p + strlen(HTTP_200), size, strlen(size));
			memcpy(msg_rsp->buf->p + strlen(HTTP_200) + strlen(size), "\r\nContent-Type: image/ipeg\r\n\r\n",
                                                                strlen("\r\nContent-Type: image/ipeg\r\n\r\n"));

			msg_rsp->buf->i = 0;
			msg_rsp->buf->o = strlen(HTTP_200) + strlen(size) + strlen("\r\nContent-Type: image/ipeg\r\n\r\n");
			msg_rsp->buf->to_forward = msg_rsp->buf->o + s->size;
			msg_rsp->buf->p += msg_rsp->buf->o;
			max = bi_avail(msg_rsp->buf);

			logging(TRACE, "[haproxy-second.cache][msg_rsp->buf->to_forward:%d]", msg_rsp->buf->to_forward);
			
            if (!max)
            {
                msg_rsp->buf->flags |= BF_FULL;
                s->si[0].flags |= SI_FL_WAIT_ROOM;
                return 1;
            }

            /*
                        * 1. compute the maximum block size we can read at once.
                        */
            if (buffer_empty(msg_rsp->buf))
            {
                /* let's realign the buffer to optimize I/O */
                msg_rsp->buf->p = msg_rsp->buf->data;
            }
            else if (msg_rsp->buf->data + msg_rsp->buf->o < msg_rsp->buf->p &&
                     msg_rsp->buf->p + msg_rsp->buf->i < msg_rsp->buf->data + msg_rsp->buf->size)
            {
                /* remaining space wraps at the end, with a moving limit */
                if (max > msg_rsp->buf->data + msg_rsp->buf->size - (msg_rsp->buf->p + msg_rsp->buf->i))
                    max = msg_rsp->buf->data + msg_rsp->buf->size - (msg_rsp->buf->p + msg_rsp->buf->i);
            }
            readl = fread(bi_end(msg_rsp->buf), sizeof(char), max, s->fp);
			logging(TRACE, "[haproxy-second.cache][msg_rsp->buf->to_forward:%d][max:%d][o:%d]", 
					msg_rsp->buf->to_forward, max, msg_rsp->buf->o);
			if (readl > 0) {
                s->offset += readl;
             //   msg_rsp->buf->o += readl;
                read_len += readl;
                msg_rsp->buf->i += readl;
                cur_read += readl;

                /* if we're allowed to directly forward data, we must update ->o */
                if (msg_rsp->buf->to_forward && !(msg_rsp->buf->flags & (BF_SHUTW|BF_SHUTW_NOW)))
                {
                    unsigned long fwd = readl;
                    if (msg_rsp->buf->to_forward != BUF_INFINITE_FORWARD)
                    {
                        if (fwd > msg_rsp->buf->to_forward)
                            fwd = msg_rsp->buf->to_forward;
                        msg_rsp->buf->to_forward -= fwd;
                    }
                    b_adv(msg_rsp->buf, fwd);
                }
				logging(TRACE, "[haproxy-second.cache][msg_rsp->buf->to_forward:%d][max:%d][o:%d]", 
					msg_rsp->buf->to_forward, max, msg_rsp->buf->o);

                logging(TRACE, "read_test:%d,size:%d,o:%d,%s\n", readl, msg_rsp->buf->size, msg_rsp->buf->o, msg_rsp->buf->data);

                if (s->offset >= s->size) {
                    logging(TRACE, "process_session:if (fp) close\n");
                    msg_req->msg_state = HTTP_MSG_TUNNEL;
                    msg_rsp->msg_state = HTTP_MSG_TUNNEL;
                    fclose(fp);
                    s->fp = NULL;
                }
                logging(TRACE, "response,%d,write:%s\n", bo_ptr(msg_rsp->buf) - msg_rsp->buf->data, bo_ptr(msg_rsp->buf));
                if (msg_rsp->buf->o) {
                    logging(TRACE, "set poll\n");
                    EV_FD_SET(s->si[0].conn.t.sock.fd, DIR_WR);
					EV_FD_SET(s->si[0].conn.t.sock.fd, DIR_RD);
                    EV_FD_CLR(s->si[1].conn.t.sock.fd, DIR_WR);
                }

                logging(TRACE,"process_session:if (fp)[msg_rsp->o:%d][size:%d][offset:%d][max:%d][readl:%d]\n",
                    msg_rsp->buf->o, s->size, s->offset, max, readl);
            } else {
				
            }
		}else{
			s->cache = -1;
			//bi_erase(msg_rsp->buf);
		}
	}
	else {
		logging(TRACE, "[haproxy-second.cache][read cache not first]");
        max = bi_avail(msg_rsp->buf);

        if (!max)
        {
            msg_rsp->buf->flags |= BF_FULL;
            s->si[0].flags |= SI_FL_WAIT_ROOM;
            return 1;
        }

        /*
	            * 1. compute the maximum block size we can read at once.
	            */
        if (buffer_empty(msg_rsp->buf))
        {
            /* let's realign the buffer to optimize I/O */
            msg_rsp->buf->p = msg_rsp->buf->data;
        }
        else if (msg_rsp->buf->data + msg_rsp->buf->o < msg_rsp->buf->p &&
                 msg_rsp->buf->p + msg_rsp->buf->i < msg_rsp->buf->data + msg_rsp->buf->size)
        {
            /* remaining space wraps at the end, with a moving limit */
            if (max > msg_rsp->buf->data + msg_rsp->buf->size - (msg_rsp->buf->p + msg_rsp->buf->i))
                max = msg_rsp->buf->data + msg_rsp->buf->size - (msg_rsp->buf->p + msg_rsp->buf->i);
        }
	    logging(TRACE, "process_session:if (fp) else[msg_rsp->o:%d][p->char:%d][max:%d]\n",
               msg_rsp->buf->o, msg_rsp->buf->p-msg_rsp->buf->data, max);

        readl = fread(bi_end(msg_rsp->buf), sizeof(char), max, s->fp);

        logging(TRACE, "readl:%d\n", readl);
        if (readl > 0) {
            s->offset += readl;
      //      msg_rsp->buf->o += readl;
            read_len += readl;
            msg_rsp->buf->i += readl;
            cur_read += readl;

            /* if we're allowed to directly forward data, we must update ->o */
            if (msg_rsp->buf->to_forward && !(msg_rsp->buf->flags & (BF_SHUTW|BF_SHUTW_NOW)))
            {
                unsigned long fwd = readl;
                if (msg_rsp->buf->to_forward != BUF_INFINITE_FORWARD)
                {
                    if (fwd > msg_rsp->buf->to_forward)
                        fwd = msg_rsp->buf->to_forward;
                    msg_rsp->buf->to_forward -= fwd;
                }
                b_adv(msg_rsp->buf, fwd);
            }

            if (s->offset >= s->size) {
                logging(TRACE, "process_session:if (fp) close\n");
                msg_req->msg_state = HTTP_MSG_TUNNEL;
                msg_rsp->msg_state = HTTP_MSG_TUNNEL;
                fclose(s->fp);
                s->fp = NULL;
            }
            logging(TRACE, "here\n");
            if (msg_rsp->buf->o) {
             
                EV_FD_SET(s->si[0].conn.t.sock.fd, DIR_WR);
				EV_FD_SET(s->si[0].conn.t.sock.fd, DIR_RD);
                EV_FD_CLR(s->si[1].conn.t.sock.fd, DIR_WR);
            }
        } else {

        }
		logging(TRACE, "process_session:if (fp) else[msg_rsp->o:%d][size:%d][offset:%d]\n", msg_rsp->buf->o, s->size, s->offset);

	}
    return 1;
}

void init_cache_file()
{
	logging(TRACE, "init_cache_file");
	char dir[100] = "/home/ww/cache/hwtestjss/images";
	struct dirent	*dirp;
	DIR             *dp;
	if ((dp = opendir(dir)) == NULL) {
		logging(TRACE, "[init_cache_file]opendir error");
		return;
	}	
	while ((dirp = readdir(dp)) != NULL) {
		if (strcmp(dirp->d_name, ".") == 0|| 
			strcmp(dirp->d_name, "..") == 0)
			continue;
		logging(TRACE, "[init_cache_file]%s", dirp->d_name);
		
	}
	
}

int process_cache_mem(struct session *s)
{
	return 0;
}

int process_cache(struct session *s)
{
	
	return process_cache_mem(s);
}


