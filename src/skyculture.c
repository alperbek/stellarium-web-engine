/* Stellarium Web Engine - Copyright (c) 2018 - Noctua Software Ltd
 *
 * This program is licensed under the terms of the GNU AGPL v3, or
 * alternatively under a commercial licence.
 *
 * The terms of the AGPL v3 license can be found in the main directory of this
 * repository.
 */

#include "swe.h"
#include <regex.h>


static constellation_infos_t *get_constellation(
        constellation_infos_t *csts, const char *id)
{
    int i;
    for (i = 0; *(csts[i].id); i++) {
        if (strcasecmp(csts[i].id, id) == 0)
            return &csts[i];
    }
    return NULL;
}

/*
 * Function: skyculture_parse_edge
 * Parse a constellation edge file.
 *
 * Parameters:
 *   data   - Text data in the edge file format.
 *   infos  - Constellation info to update with the edge data.
 *
 * Return:
 *   The number of lines parsed, or -1 in case of error.
 */
int skyculture_parse_edges(const char *edges, constellation_infos_t *csts)
{
    constellation_infos_t *info = NULL;
    const char *line;
    char cst[2][8];
    int i, ra1_h, ra1_m, ra1_s, ra2_h, ra2_m, ra2_s, nb;
    char dec1_sign, dec2_sign;
    int dec1_d, dec1_m, dec1_s, dec2_d, dec2_m, dec2_s;
    double ra1, dec1, ra2, dec2;
    const int MAX_EDGES = ARRAY_SIZE(info->edges);

    for (line = edges, nb = 0; *line; line = strchr(line, '\n') + 1, nb++) {
        if (str_startswith(line, "//")) continue;
        if (*line == '\n') continue;
        if (sscanf(line, "%*s %*s"
                         "%d:%d:%d %c%d:%d:%d "
                         "%d:%d:%d %c%d:%d:%d "
                         "%s %s",
                         &ra1_h, &ra1_m, &ra1_s,
                         &dec1_sign, &dec1_d, &dec1_m, &dec1_s,
                         &ra2_h, &ra2_m, &ra2_s,
                         &dec2_sign, &dec2_d, &dec2_m, &dec2_s,
                         cst[0], cst[1]) != 16) {
            LOG_W("Cannot parse skyculture edge line: %.16s...", line);
            continue;
        }
        eraTf2a('+', ra1_h, ra1_m, ra1_s, &ra1);
        eraTf2a('+', ra2_h, ra2_m, ra2_s, &ra2);
        eraAf2a(dec1_sign, dec1_d, dec1_m, dec1_s, &dec1);
        eraAf2a(dec2_sign, dec2_d, dec2_m, dec2_s, &dec2);
        for (i = 0; i < 2; i++) {
            info = get_constellation(csts, cst[i]);
            if (!info) continue;
            if (info->nb_edges >= MAX_EDGES) {
                LOG_E("Too many bounds in constellation %s", cst);
                continue;
            }
            info->edges[info->nb_edges][0][0] = ra1;
            info->edges[info->nb_edges][0][1] = dec1;
            info->edges[info->nb_edges][1][0] = ra2;
            info->edges[info->nb_edges][1][1] = dec2;
            info->nb_edges++;
        }
    }
    return nb;
}

static int parse_lines_json(const json_value *v, int lines[64][2])
{
    int i, j, n, nb = 0;
    const json_value *seg;

    if (v->type != json_array) return -1;
    for (i = 0; i < v->u.array.length; i++) {
        seg = v->u.array.values[i];
        if (seg->type != json_array) return -1;
        n = seg->u.array.length - 1;
        for (j = 0; j < n; j++) {
            lines[nb][0] = seg->u.array.values[j]->u.integer;
            lines[nb][1] = seg->u.array.values[j + 1]->u.integer;
            nb++;
        }
    }
    return nb;
}

int skyculture_parse_feature_json(const json_value *v,
                                  constellation_infos_t *feature)
{
    const char *id, *name;
    int r;
    const json_value *lines = NULL, *description = NULL;

    r = jcon_parse(v, "{",
        "!id", JCON_STR(id),
        "name", JCON_STR(name),
        "lines", JCON_VAL(lines),
        "description", JCON_VAL(description),
    "}");
    if (r) goto error;

    snprintf(feature->id, sizeof(feature->id), "%s", id);
    if (name)
        snprintf(feature->name, sizeof(feature->name), "%s", name);
    if (description)
        feature->description = json_to_string(description);

    if (lines) {
        feature->nb_lines = parse_lines_json(lines, feature->lines);
        if (feature->nb_lines < 0) goto error;
    }

    return 0;

error:
    LOG_E("Cannot parse json feature");
    return -1;
}

// Eventually to be merged with skyculture_parse_feature_json.
int skyculture_parse_feature_art_json(const json_value *v,
                                      constellation_art_t *art)
{
    int i, r, w, h, x, y, hip;
    json_value *anchors[3];
    const char *id, *img;

    id = json_get_attr_s(v, "id");
    if (!id) goto error;
    snprintf(art->cst, sizeof(art->cst), "%s", id);

    v = json_get_attr(v, "image", json_object);
    if (!v) return -1;

    r = jcon_parse(v, "{",
        "!file", JCON_STR(img),
        "!size", "[", JCON_INT(w), JCON_INT(h), "]",
        "!anchors", "[",
            JCON_VAL(anchors[0]),
            JCON_VAL(anchors[1]),
            JCON_VAL(anchors[2]),
        "]",
    "}");
    if (r) goto error;

    snprintf(art->img, sizeof(art->img), "%s", img);
    art->uv_in_pixel = false;

    for (i = 0; i < 3; i++) {
        r = jcon_parse(anchors[i], "{",
            "pos", "[", JCON_INT(x), JCON_INT(y), "]",
            "hip", JCON_INT(hip),
        "}");
        if (r) goto error;
        art->anchors[i].hip = hip;
        art->anchors[i].uv[0] = (double)x / w;
        art->anchors[i].uv[1] = (double)y / h;
    }
    return 0;

error:
    LOG_E("Cannot parse json feature");
    return -1;
}

skyculture_name_t *skyculture_parse_names_json(const json_value *v)
{
    int i, j;
    const char *key, *name;
    uint64_t oid;
    skyculture_name_t *ret = NULL, *entry;

    if (v->type != json_object) goto error;
    for (i = 0; i < v->u.object.length; i++) {
        oid = 0;
        key = v->u.object.values[i].name;
        if (strncmp(key, "HIP ", 4) == 0)
            oid = oid_create("HIP", atoi(key + 4));
        if (!oid) goto error;

        if (v->u.object.values[i].value->type != json_array)
            goto error;

        for (j = 0; j < v->u.object.values[i].value->u.array.length; j++) {
            name = v->u.object.values[i].value->u.array.values[j]->u.string.ptr;
            entry = calloc(1, sizeof(*entry));
            entry->oid = oid;
            snprintf(entry->name, sizeof(entry->name), "%s", name);
            HASH_ADD(hh, ret, oid, sizeof(entry->oid), entry);

            // Ignore alternative names for the moment!
            break;
        }
    }

    return ret;

error:

    LOG_E("Cannot parse skyculture names");
    return NULL;
}
