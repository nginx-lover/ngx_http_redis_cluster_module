/*
 * Copyright detailyang
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


static void * ngx_http_redis_cluster_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_redis_cluster_enable(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_redis_cluster_handler(ngx_http_request_t *r);

typedef struct {
    ngx_flag_t enable;
} ngx_http_redis_cluster_loc_conf_t;


static ngx_command_t ngx_http_redis_cluster_commands[] = {
    {
        ngx_string("redis_cluster_enable"),
        NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
        ngx_http_redis_cluster_enable,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_redis_cluster_loc_conf_t, enable),
        NULL
    },
    ngx_null_command
};


static ngx_http_module_t ngx_http_redis_cluster_module_ctx = {
    NULL,                                   /* preconfiguration */
    NULL,                                   /* postconfiguration */
    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    ngx_http_redis_cluster_create_loc_conf, /* create location configration */
    NULL                                    /* merge location configration */
};


ngx_module_t ngx_http_redis_cluster_module = {
    NGX_MODULE_V1,
    &ngx_http_redis_cluster_module_ctx,     /* module context */
    ngx_http_redis_cluster_commands,        /* module directives */
    NGX_HTTP_MODULE,                        /* module type */
    NULL,                                   /* init master */
    NULL,                                   /* init module */
    NULL,                                   /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    NULL,                                   /* exit process */
    NULL,                                   /* exit master */
    NGX_MODULE_V1_PADDING
};


static void *
ngx_http_redis_cluster_create_loc_conf(ngx_conf_t *cf) {
    ngx_http_redis_cluster_loc_conf_t *hrclcf;

    hrclcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_redis_cluster_loc_conf_t));
    if (hrclcf == NULL) {
        return NGX_CONF_ERROR;
    }
    /*
        set by ngx_pcalloc
        hrclcf->enable = 0;
    */
    hrclcf->enable = NGX_CONF_UNSET;

    return hrclcf;
}


static char *
ngx_http_redis_cluster_enable(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_redis_cluster_handler;

    return ngx_conf_set_flag_slot(cf, cmd, conf);
}


static ngx_int_t
ngx_http_redis_cluster_handler(ngx_http_request_t *r) {
    ngx_int_t                          rc;
    ngx_buf_t                         *b;
    ngx_chain_t                        out;
    ngx_http_redis_cluster_loc_conf_t *hrclcf;

    hrclcf = ngx_http_get_module_loc_conf(r, ngx_http_redis_cluster_module);

    r->headers_out.status = NGX_HTTP_OK;
    ngx_str_set(&r->headers_out.content_type, "text/html");
    r->headers_out.content_length_n = 0;

    if (hrclcf->enable == 0) {
        // send header only
        r->header_only = 1;
        r->headers_out.status = NGX_HTTP_NO_CONTENT;

        return rc = ngx_http_send_header(r);
    }

    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    out.buf = b;
    out.next = NULL;

    b->pos = (void *)"abcd";
    b->last = b->pos + 4;
    b->memory = 1;
    b->last_buf = 1;

    r->headers_out.content_length_n = 4;

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    return ngx_http_output_filter(r, &out);
}
