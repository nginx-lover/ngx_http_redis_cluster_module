#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


static void *
ngx_http_redis_cluster_create_loc_conf(ngx_conf_t *cf);
static ngx_int_t
ngx_http_redis_cluster_handler(ngx_http_request_t *r);
static char *
ngx_http_redis_cluster_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t
ngx_http_redis_cluster_create_request(ngx_http_request_t *r);
static ngx_int_t
ngx_http_redis_cluster_reinit_request(ngx_http_request_t *r);
static ngx_int_t
ngx_http_redis_cluster_process_header(ngx_http_request_t *r);
static ngx_int_t
ngx_http_redis_cluster_filter_init(void *data);
static ngx_int_t
ngx_http_redis_cluster_filter(void *data, ssize_t bytes);
static void
ngx_http_redis_cluster_abort_request(ngx_http_request_t *r);
static void
ngx_http_redis_cluster_finalize_request(ngx_http_request_t *r, ngx_int_t rc);
static char *
ngx_http_redis_cluster_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);


typedef struct {
    ngx_http_upstream_conf_t     upstream;
    ngx_http_complex_value_t    *complex_target;
} ngx_http_redis_cluster_loc_conf_t;


typedef struct {
    ngx_http_request_t *request;
    int                 state;
} ngx_http_redis_cluster_ctx_t;


static ngx_command_t ngx_http_redis_cluster_commands[] = {
    {
        ngx_string("redis_cluster_pass"),
        NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
        ngx_http_redis_cluster_pass,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
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
    ngx_http_redis_cluster_merge_loc_conf   /* merge location configration */
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
     * set by ngx_pcalloc():
     *
     *     conf->upstream.bufs.num = 0;
     *     conf->upstream.next_upstream = 0;
     *     conf->upstream.temp_path = NULL;
     *     conf->upstream.uri = { 0, NULL };
     *     conf->upstream.location = NULL;
     */

    hrclcf->upstream.connect_timeout = NGX_CONF_UNSET_MSEC;
    hrclcf->upstream.send_timeout = NGX_CONF_UNSET_MSEC;
    hrclcf->upstream.read_timeout = NGX_CONF_UNSET_MSEC;

    hrclcf->upstream.buffer_size = NGX_CONF_UNSET_SIZE;

    /* the hardcoded values */
    hrclcf->upstream.cyclic_temp_file = 0;
    hrclcf->upstream.buffering = 0;
    hrclcf->upstream.ignore_client_abort = 1;
    hrclcf->upstream.send_lowat = 0;
    hrclcf->upstream.bufs.num = 0;
    hrclcf->upstream.busy_buffers_size = 0;
    hrclcf->upstream.max_temp_file_size = 0;
    hrclcf->upstream.temp_file_write_size = 0;
    hrclcf->upstream.intercept_errors = 1;
    hrclcf->upstream.intercept_404 = 1;
    hrclcf->upstream.pass_request_headers = 0;
    hrclcf->upstream.pass_request_body = 0;

    return hrclcf;
}


static char *
ngx_http_redis_cluster_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_redis_cluster_loc_conf_t *prev = parent;
    ngx_http_redis_cluster_loc_conf_t *conf = child;

    ngx_conf_merge_msec_value(conf->upstream.connect_timeout,
                              prev->upstream.connect_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.send_timeout,
                              prev->upstream.send_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.read_timeout,
                              prev->upstream.read_timeout, 60000);

    ngx_conf_merge_size_value(conf->upstream.buffer_size,
                              prev->upstream.buffer_size,
                              (size_t) ngx_pagesize);

    ngx_conf_merge_bitmask_value(conf->upstream.next_upstream,
                              prev->upstream.next_upstream,
                              (NGX_CONF_BITMASK_SET
                               |NGX_HTTP_UPSTREAM_FT_ERROR
                               |NGX_HTTP_UPSTREAM_FT_TIMEOUT));

    if (conf->upstream.next_upstream & NGX_HTTP_UPSTREAM_FT_OFF) {
        conf->upstream.next_upstream = NGX_CONF_BITMASK_SET
                                       |NGX_HTTP_UPSTREAM_FT_OFF;
    }

    if (conf->upstream.upstream == NULL) {
        conf->upstream.upstream = prev->upstream.upstream;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_redis_cluster_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_redis_cluster_loc_conf_t *hrclcf = conf;

    ngx_str_t                               *value;
    ngx_uint_t                               n;
    ngx_url_t                                url;
    ngx_http_core_loc_conf_t                *clcf;
    ngx_http_compile_complex_value_t         ccv;

    if (hrclcf->upstream.upstream) {
        return "is duplicate";
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    clcf->handler = ngx_http_redis_cluster_handler;

    if (clcf->name.data[clcf->name.len - 1] == '/') {
        clcf->auto_redirect = 1;
    }

    value = cf->args->elts;

    n = ngx_http_script_variables_count(&value[1]);
    if (n) {
        hrclcf->complex_target = ngx_palloc(cf->pool,
                                          sizeof(ngx_http_complex_value_t));

        if (hrclcf->complex_target == NULL) {
            return NGX_CONF_ERROR;
        }

        ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));
        ccv.cf = cf;
        ccv.value = &value[1];
        ccv.complex_value = hrclcf->complex_target;

        if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        return NGX_CONF_OK;
    }

    hrclcf->complex_target = NULL;

    ngx_memzero(&url, sizeof(ngx_url_t));

    url.url = value[1];
    url.no_resolve = 1;

    hrclcf->upstream.upstream = ngx_http_upstream_add(cf, &url, 0);
    if (hrclcf->upstream.upstream == NULL) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_redis_cluster_handler(ngx_http_request_t *r) {
    ngx_int_t                          rc;
    ngx_buf_t                         *b;
    ngx_chain_t                        out;
    ngx_http_upstream_t               *u;
    ngx_http_redis_cluster_ctx_t      *ctx;
    ngx_http_redis_cluster_loc_conf_t *hrclcf;

    hrclcf = ngx_http_get_module_loc_conf(r, ngx_http_redis_cluster_module);

    if (ngx_http_upstream_create(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    u = r->upstream;

    ngx_str_set(&u->schema, "redis-cluster://");
    u->output.tag = (ngx_buf_tag_t) &ngx_http_redis_cluster_module;

    u->conf = &hrclcf->upstream;

    u->create_request = ngx_http_redis_cluster_create_request;
    u->reinit_request = ngx_http_redis_cluster_reinit_request;
    u->process_header = ngx_http_redis_cluster_process_header;
    u->abort_request = ngx_http_redis_cluster_abort_request;
    u->finalize_request = ngx_http_redis_cluster_finalize_request;

    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_redis_cluster_ctx_t));
    if (ctx == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ctx->request = r;
    ctx->state = NGX_ERROR;

    ngx_http_set_ctx(r, ctx, ngx_http_redis_cluster_module);

    u->input_filter_init = ngx_http_redis_cluster_filter_init;
    u->input_filter = ngx_http_redis_cluster_filter;
    u->input_filter_ctx = ctx;

    rc = ngx_http_read_client_request_body(r, ngx_http_upstream_init);

    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    return NGX_DONE;
}



static ngx_int_t
ngx_http_redis_cluster_create_request(ngx_http_request_t *r)
{

    return NGX_OK;
}


static ngx_int_t
ngx_http_redis_cluster_reinit_request(ngx_http_request_t *r)
{
    return NGX_OK;
}


static ngx_int_t
ngx_http_redis_cluster_process_header(ngx_http_request_t *r)
{
    return NGX_OK;
}


static ngx_int_t
ngx_http_redis_cluster_filter_init(void *data)
{
#if 0
    ngx_http_redis_cluster_ctx_t  *ctx = data;
    ngx_http_upstream_t  *u;
    u = ctx->request->upstream;
#endif

    return NGX_OK;
}


static ngx_int_t
ngx_http_redis_cluster_filter(void *data, ssize_t bytes)
{
    ngx_http_redis_cluster_ctx_t  *ctx = data;

    // return ctx->filter(ctx, bytes);
    return NGX_OK;
}


static void
ngx_http_redis_cluster_abort_request(ngx_http_request_t *r)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "abort http redis_cluster request");
    return;
}


static void
ngx_http_redis_cluster_finalize_request(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "finalize http redis_cluster request");

    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        r->headers_out.status = rc;
    }

    return;
}
