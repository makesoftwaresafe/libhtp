/***************************************************************************
 * Copyright 2009-2010 Open Information Security Foundation
 * Copyright 2010-2011 Qualys, Inc.
 *
 * Licensed to You under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ***************************************************************************/

/**
 * @file
 * @author Ivan Ristic <ivanr@webkreator.com>
 */

#include "htp.h"

/**
 * Creates a new transaction structure.
 *
 * @param cfg
 * @param is_cfg_shared
 * @param conn
 * @return The newly created transaction, or NULL on memory allocation failure.
 */
htp_tx_t *htp_tx_create(htp_cfg_t *cfg, int is_cfg_shared, htp_conn_t *conn) {
    htp_tx_t *tx = calloc(1, sizeof (htp_tx_t));
    if (tx == NULL) return NULL;

    tx->conn = conn;
    tx->cfg = cfg;
    tx->is_cfg_shared = is_cfg_shared;

    tx->conn = conn;

    tx->request_header_lines = list_array_create(32);
    tx->request_headers = table_create(32);
    tx->request_line_nul_offset = -1;
    tx->parsed_uri = calloc(1, sizeof (htp_uri_t));
    tx->parsed_uri_incomplete = calloc(1, sizeof (htp_uri_t));

    tx->response_header_lines = list_array_create(32);
    tx->response_headers = table_create(32);

    tx->request_protocol_number = -1;

    return tx;
}

/**
 * Destroys the supplied transaction.
 *
 * @param tx
 */
void htp_tx_destroy(htp_tx_t *tx) {
    bstr_free(&tx->request_line);
    bstr_free(&tx->request_line_raw);
    bstr_free(&tx->request_method);
    bstr_free(&tx->request_uri);
    bstr_free(&tx->request_uri_normalized);
    bstr_free(&tx->request_protocol);
    bstr_free(&tx->request_headers_sep);

    if (tx->parsed_uri != NULL) {
        bstr_free(&tx->parsed_uri->scheme);
        bstr_free(&tx->parsed_uri->username);
        bstr_free(&tx->parsed_uri->password);
        bstr_free(&tx->parsed_uri->hostname);
        bstr_free(&tx->parsed_uri->port);
        bstr_free(&tx->parsed_uri->path);
        bstr_free(&tx->parsed_uri->query);
        bstr_free(&tx->parsed_uri->fragment);

        free(tx->parsed_uri);
    }

    if (tx->parsed_uri_incomplete != NULL) {
        bstr_free(&tx->parsed_uri_incomplete->scheme);
        bstr_free(&tx->parsed_uri_incomplete->username);
        bstr_free(&tx->parsed_uri_incomplete->password);
        bstr_free(&tx->parsed_uri_incomplete->hostname);
        bstr_free(&tx->parsed_uri_incomplete->port);
        bstr_free(&tx->parsed_uri_incomplete->path);
        bstr_free(&tx->parsed_uri_incomplete->query);
        bstr_free(&tx->parsed_uri_incomplete->fragment);
        free(tx->parsed_uri_incomplete);
    }

    // Destroy request_header_lines
    htp_header_line_t *hl = NULL;
    list_iterator_reset(tx->request_header_lines);
    while ((hl = list_iterator_next(tx->request_header_lines)) != NULL) {
        bstr_free(&hl->line);
        // No need to destroy hl->header because
        // htp_header_line_t does not own it.
        free(hl);
    }

    list_destroy(&tx->request_header_lines);

    // Destroy request_headers    
    htp_header_t *h = NULL;
    table_iterator_reset(tx->request_headers);
    while (table_iterator_next(tx->request_headers, (void **) & h) != NULL) {
        bstr_free(&h->name);
        bstr_free(&h->value);
        free(h);
    }

    table_destroy(&tx->request_headers);

    if (tx->request_headers_raw != NULL) {
        bstr_free(&tx->request_headers_raw);
    }

    bstr_free(&tx->response_line);
    bstr_free(&tx->response_protocol);
    bstr_free(&tx->response_status);
    bstr_free(&tx->response_message);
    bstr_free(&tx->response_headers_sep);

    // Destroy response_header_lines
    hl = NULL;
    list_iterator_reset(tx->response_header_lines);
    while ((hl = list_iterator_next(tx->response_header_lines)) != NULL) {
        bstr_free(&hl->line);
        // No need to destroy hl->header because
        // htp_header_line_t does not own it.
        free(hl);
    }
    list_destroy(&tx->response_header_lines);

    // Destroy response headers    
    h = NULL;
    table_iterator_reset(tx->response_headers);
    while (table_iterator_next(tx->response_headers, (void **) & h) != NULL) {
        bstr_free(&h->name);
        bstr_free(&h->value);
        free(h);
    }
    table_destroy(&tx->response_headers);

    // Tell the connection to remove this transaction
    // from the list
    htp_conn_remove_tx(tx->conn, tx);

    // Invalidate the pointer to this transactions held
    // by the connection parser. This is to allow a transaction
    // to be destroyed from within the final response callback.
    if (tx->connp != NULL) {
        if (tx->connp->out_tx == tx) {
            tx->connp->out_tx = NULL;
        }
    }
    
    bstr_free(&tx->request_content_type);

    htp_urlenp_destroy(&tx->request_urlenp_query);
    htp_urlenp_destroy(&tx->request_urlenp_body);
    htp_mpartp_destroy(&tx->request_mpartp);

    if ((tx->request_params_query_reused == 0)&&(tx->request_params_query != NULL)) {
        bstr *val = NULL;
        table_iterator_reset(tx->request_params_query);
        while(table_iterator_next(tx->request_params_query, (void **) &val) != NULL) {
            bstr_free(&val);
        }
        table_destroy(&tx->request_params_query);
    }

    if ((tx->request_params_body_reused == 0)&&(tx->request_params_body != NULL)) {
        bstr *val = NULL;
        table_iterator_reset(tx->request_params_body);
        while(table_iterator_next(tx->request_params_body, (void **) &val) != NULL) {
            bstr_free(&val);
        }
        table_destroy(&tx->request_params_body);
    }

    table_destroy(&tx->request_cookies);

    hook_destroy(tx->hook_request_body_data);

    free(tx);
}

/**
 * Returns the user data associated with this transaction. 
 *
 * @param tx
 * @return A pointer to user data or NULL
 */
void *htp_tx_get_user_data(htp_tx_t *tx) {
    return tx->user_data;
}

/**
 * Sets the configuration that is to be used for this transaction.
 *
 * @param tx
 * @param cfg
 * @param is_cfg_shared
 */
void htp_tx_set_config(htp_tx_t *tx, htp_cfg_t *cfg, int is_cfg_shared) {
    tx->cfg = cfg;
    tx->is_cfg_shared = is_cfg_shared;
}

/**
 * Associates user data with this transaction.
 *
 * @param tx
 * @param user_data
 */
void htp_tx_set_user_data(htp_tx_t *tx, void *user_data) {
    tx->user_data = user_data;
}

/**
 * Register callback for the transaction-specific REQUEST_BODY_DATA hook.
 *
 * @param tx
 * @pram callback_fn
 */
void htp_tx_register_request_body_data(htp_tx_t *tx, int (*callback_fn)(htp_tx_data_t *)) {
    hook_register(&tx->hook_request_body_data, (htp_callback_fn_t)callback_fn);
}

/**
 * Register callback for the transaction-specific RESPONSE_BODY_DATA hook.
 *
 * @param tx
 * @pram callback_fn
 */
void htp_tx_register_response_body_data(htp_tx_t *tx, int (*callback_fn)(htp_tx_data_t *)) {
    hook_register(&tx->hook_response_body_data, (htp_callback_fn_t)callback_fn);
}
