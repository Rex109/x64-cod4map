
#include "cod4map.h"
#include "mesh.h"


static const int s_neighbors[NUM_NEIGHBORS][2] =                        /* 0x004fc960 */
{
    {  0,  1 }, {  1,  1 }, {  1,  0 }, {  1, -1 },
    {  0, -1 }, { -1, -1 }, { -1,  0 }, { -1,  1 }
};

MeshVert_t expand[MAX_EXPANDED_AXIS][MAX_EXPANDED_AXIS];                /* 0x123ce908 */


static void SubdivideMeshColumn( Mesh_t *mesh, int col, float maxError,
                                 float minLength, int *widthTable );
static void SubdivideMeshRow( Mesh_t *mesh, int row, float maxError,
                              float minLength, int *heightTable );


/* LerpDrawVert  0x00426320 */
void LerpDrawVert( const MeshVert_t *a, const MeshVert_t *b, MeshVert_t *out )
{
    out->xyz[0] = ( a->xyz[0] + b->xyz[0] ) * 0.5f;
    out->xyz[1] = ( a->xyz[1] + b->xyz[1] ) * 0.5f;
    out->xyz[2] = ( a->xyz[2] + b->xyz[2] ) * 0.5f;

    out->st[0]  = ( a->st[0]  + b->st[0]  ) * 0.5f;
    out->st[1]  = ( a->st[1]  + b->st[1]  ) * 0.5f;

    out->lmSt[0] = ( a->lmSt[0] + b->lmSt[0] ) * 0.5f;
    out->lmSt[1] = ( a->lmSt[1] + b->lmSt[1] ) * 0.5f;

    out->color[0] = ( byte )( ( a->color[0] + b->color[0] ) >> 1 );
    out->color[1] = ( byte )( ( a->color[1] + b->color[1] ) >> 1 );
    out->color[2] = ( byte )( ( a->color[2] + b->color[2] ) >> 1 );
    out->color[3] = ( byte )( ( a->color[3] + b->color[3] ) >> 1 );
}


/* FreeMesh  0x00426430 */
void FreeMesh( void *mesh )
{
    free( mesh );
}


/* CopyMesh  0x00426450 */
Mesh_t *CopyMesh( const Mesh_t *src )
{
    Mesh_t      *out;
    unsigned int vertSize;

    vertSize = src->width * src->height * sizeof( MeshVert_t );

    out         = ( Mesh_t * )malloc( sizeof( Mesh_t ) + vertSize );
    out->width  = src->width;
    out->height = src->height;
    out->verts  = ( MeshVert_t * )( out + 1 );

    memcpy( out->verts, src->verts, vertSize );

    return out;
}


/* TransposeMesh  0x004264c0 */
Mesh_t *TransposeMesh( Mesh_t *inMesh )
{
    int     w, h;
    Mesh_t *out;

    out         = ( Mesh_t * )malloc( sizeof( Mesh_t ) );
    out->width  = inMesh->height;
    out->height = inMesh->width;
    out->verts  = ( MeshVert_t * )malloc( out->width * out->height * sizeof( MeshVert_t ) );

    for ( h = 0; h < inMesh->height; h++ )
    {
        for ( w = 0; w < inMesh->width; w++ )
        {
            out->verts[w * inMesh->height + h] = inMesh->verts[h * inMesh->width + w];
        }
    }

    FreeMesh( inMesh );

    return out;
}


/* MirrorMesh  0x004265a0 */
void MirrorMesh( Mesh_t *mesh )
{
    int        w, h;
    MeshVert_t temp;

    for ( h = 0; h < mesh->height; h++ )
    {
        for ( w = 0; w < mesh->width / 2; w++ )
        {
            temp = mesh->verts[h * mesh->width + w];
            mesh->verts[h * mesh->width + w] =
                mesh->verts[h * mesh->width + mesh->width - 1 - w];
            mesh->verts[h * mesh->width + mesh->width - 1 - w] = temp;
        }
    }
}


/* MakeMeshNormals  0x004266a0 */
void MakeMeshNormals( Mesh_t *inMesh )
{
    int         i, j, k, dist;
    int         x, y;
    int         wrapWidth, wrapHeight;
    int         contributors;
    int         good[NUM_NEIGHBORS];
    vec3_t      around[NUM_NEIGHBORS];
    vec3_t      base, delta, temp, normal, sum;
    float       len;
    MeshVert_t *dv;

    wrapWidth = 0;
    for ( i = 0; i < inMesh->height; i++ )
    {
        Vec3Sub( inMesh->verts[i * inMesh->width].xyz,
                 inMesh->verts[i * inMesh->width + inMesh->width - 1].xyz, delta );
        len = Vec3Length( delta );
        if ( len > 1.0 )
        {
            break;
        }
    }
    if ( i == inMesh->height )
    {
        wrapWidth = 1;
    }

    wrapHeight = 0;
    for ( i = 0; i < inMesh->width; i++ )
    {
        Vec3Sub( inMesh->verts[i].xyz,
                 inMesh->verts[( inMesh->height - 1 ) * inMesh->width + i].xyz, delta );
        len = Vec3Length( delta );
        if ( len > 1.0 )
        {
            break;
        }
    }
    if ( i == inMesh->width )
    {
        wrapHeight = 1;
    }

    for ( i = 0; i < inMesh->width; i++ )
    {
        for ( j = 0; j < inMesh->height; j++ )
        {
            contributors = 0;

            dv = &inMesh->verts[j * inMesh->width + i];
            Vec3Copy( dv->xyz, base );

            for ( k = 0; k < NUM_NEIGHBORS; k++ )
            {
                Vec3Clear( around[k] );
                good[k] = 0;

                for ( dist = 1; dist < 4; dist++ )
                {
                    x = s_neighbors[k][0] * dist + i;
                    y = s_neighbors[k][1] * dist + j;

                    if ( wrapWidth )
                    {
                        if ( x < 0 )
                        {
                            x = inMesh->width - 1 + x;
                        }
                        else if ( x >= inMesh->width )
                        {
                            x = x + 1 - inMesh->width;
                        }
                    }

                    if ( wrapHeight )
                    {
                        if ( y < 0 )
                        {
                            y = inMesh->height - 1 + y;
                        }
                        else if ( y >= inMesh->height )
                        {
                            y = y + 1 - inMesh->height;
                        }
                    }

                    if ( x < 0 || x >= inMesh->width || y < 0 || y >= inMesh->height )
                    {
                        break;
                    }

                    Vec3Sub( inMesh->verts[y * inMesh->width + x].xyz, base, temp );

                    if ( Vec3Normalize( temp ) != 0.0 )
                    {
                        good[k] = 1;
                        Vec3Copy( temp, around[k] );
                        break;
                    }
                }
            }

            Vec3Clear( sum );

            for ( k = 0; k < NUM_NEIGHBORS; k++ )
            {
                if ( good[k] && good[( k + 1 ) & 7] )
                {
                    Vec3Cross( around[( k + 1 ) & 7], around[k], normal );

                    if ( Vec3Normalize( normal ) != 0.0 )
                    {
                        Vec3Add( normal, sum, sum );
                        contributors++;
                    }
                }
            }

            if ( contributors == 0 )
            {
                contributors = 1;
            }

            Vec3NormalizeTo( sum, dv->normal );
        }
    }
}


/* GuessPatchPlane  0x00426b30 */
int GuessPatchPlane( const Mesh_t *inMesh, vec4_t plane )
{
    vec3_t sumNormal;
    vec3_t sumPoint;
    vec3_t edge1, edge2, faceNormal;
    float  avgDist;
    float  dotDist;
    int    numPlanes;
    int    row, col;

    Vec3Clear( sumNormal );
    Vec3Clear( sumPoint );
    numPlanes = 0;

    for ( row = 0; row < inMesh->height - 1; row++ )
    {
        for ( col = 0; col < inMesh->width - 1; col++ )
        {
            Vec3Sub( inMesh->verts[row * inMesh->width + col + 1].xyz,
                     inMesh->verts[row * inMesh->width + col].xyz, edge1 );
            Vec3Sub( inMesh->verts[( row + 1 ) * inMesh->width + col].xyz,
                     inMesh->verts[row * inMesh->width + col].xyz, edge2 );
            Vec3Cross( edge1, edge2, faceNormal );

            if ( Vec3Normalize( faceNormal ) > MIN_NORMAL_LENGTH )
            {
                Vec3Add( sumNormal, faceNormal, sumNormal );
                Vec3Add( sumPoint, inMesh->verts[row * inMesh->width + col].xyz, sumPoint );
                numPlanes++;
            }

            Vec3Sub( inMesh->verts[( row + 1 ) * inMesh->width + col].xyz,
                     inMesh->verts[( row + 1 ) * inMesh->width + col + 1].xyz, edge1 );
            Vec3Sub( inMesh->verts[row * inMesh->width + col + 1].xyz,
                     inMesh->verts[( row + 1 ) * inMesh->width + col + 1].xyz, edge2 );
            Vec3Cross( edge1, edge2, faceNormal );

            if ( Vec3Normalize( faceNormal ) > MIN_NORMAL_LENGTH )
            {
                Vec3Add( sumNormal, faceNormal, sumNormal );
                Vec3Add( sumPoint,
                         inMesh->verts[( row + 1 ) * inMesh->width + col + 1].xyz,
                         sumPoint );
                numPlanes++;
            }
        }
    }

    if ( Vec3Normalize( sumNormal ) < MIN_NORMAL_LENGTH )
    {
        return 0;
    }

    avgDist = Vec3Dot( sumPoint, sumNormal ) / numPlanes;

    for ( col = 0; col < inMesh->width - 1; col++ )
    {
        for ( row = 0; row < inMesh->height - 1; row++ )
        {
            dotDist = Vec3Dot( sumNormal, inMesh->verts[row * inMesh->width + col].xyz )
                      - avgDist;

            if ( dotDist > 1.0 || dotDist < -1.0 )
            {
                return 0;
            }
        }
    }

    if ( plane )
    {
        Vec3Copy( sumNormal, plane );
        plane[3] = avgDist;
    }

    return 1;
}


/* PutMeshOnCurve  0x00426e80 */
void PutMeshOnCurve( Mesh_t mesh )
{
    MeshVert_t  prev, next;
    MeshVert_t *cur;
    int         i, j;

    for ( i = 0; i < mesh.width; i++ )
    {
        for ( j = 1; j < mesh.height; j += 2 )
        {
            cur = &mesh.verts[j * mesh.width + i];
            LerpDrawVert( cur, &mesh.verts[( j + 1 ) * mesh.width + i], &prev );
            LerpDrawVert( cur, &mesh.verts[( j - 1 ) * mesh.width + i], &next );
            LerpDrawVert( &prev, &next, cur );
        }
    }

    for ( j = 0; j < mesh.height; j++ )
    {
        for ( i = 1; i < mesh.width; i += 2 )
        {
            cur = &mesh.verts[j * mesh.width + i];
            LerpDrawVert( cur, &mesh.verts[j * mesh.width + i + 1], &prev );
            LerpDrawVert( cur, &mesh.verts[j * mesh.width + i - 1], &next );
            LerpDrawVert( &prev, &next, cur );
        }
    }
}


/* SubdivideMesh  0x00427040 */
Mesh_t *SubdivideMesh( Mesh_t inMesh, float maxError, float minLength,
                       int *widthTable, int *heightTable )
{
    Mesh_t mesh;
    int    i, j;

    mesh.width  = inMesh.width;
    mesh.height = inMesh.height;

    for ( i = 0; i < inMesh.width; i++ )
    {
        for ( j = 0; j < inMesh.height; j++ )
        {
            expand[j][i] = inMesh.verts[j * inMesh.width + i];
        }
    }

    if ( heightTable )
    {
        for ( i = 0; i < inMesh.height; i++ )
        {
            heightTable[i] = i;
        }
    }

    if ( widthTable )
    {
        for ( i = 0; i < inMesh.width; i++ )
        {
            widthTable[i] = i;
        }
    }

    for ( j = 0; j + 2 < mesh.width; j += 2 )
    {
        SubdivideMeshColumn( &mesh, j, maxError, minLength, widthTable );
    }

    for ( j = 0; j + 2 < mesh.height; j += 2 )
    {
        SubdivideMeshRow( &mesh, j, maxError, minLength, heightTable );
    }

    mesh.verts = &expand[0][0];
    for ( i = 1; i < mesh.height; i++ )
    {
        memmove( mesh.verts + i * mesh.width, &expand[i][0],
                 mesh.width * sizeof( MeshVert_t ) );
    }

    return CopyMesh( &mesh );
}


/* SubdivideMeshColumn  0x00427200 */
static void SubdivideMeshColumn( Mesh_t *mesh, int col, float maxError,
                                 float minLength, int *widthTable )
{
    MeshVert_t prev, next, mid;
    vec3_t     prevVec, nextVec, midVec;
    float      error;
    int        j, k;

    while ( mesh->width + 2 < MAX_EXPANDED_AXIS )
    {
        for ( j = 0; j < mesh->height; j++ )
        {
            Vec3Sub( expand[j][col + 1].xyz, expand[j][col].xyz, prevVec );
            if ( Vec3Length( prevVec ) > minLength )
            {
                break;
            }

            Vec3Sub( expand[j][col + 2].xyz, expand[j][col + 1].xyz, nextVec );
            if ( Vec3Length( nextVec ) > minLength )
            {
                break;
            }

            Vec3Sub( nextVec, prevVec, midVec );
            error = Vec3Length( midVec ) * 0.25f;
            if ( error > maxError )
            {
                break;
            }
        }

        if ( j == mesh->height )
        {
            break;
        }

        mesh->width += 2;

        if ( widthTable )
        {
            for ( k = mesh->width - 1; k > col + 3; k-- )
            {
                widthTable[k] = widthTable[k - 2];
            }
            widthTable[col + 3] = widthTable[col + 1];
            widthTable[col + 2] = widthTable[col + 1];
            widthTable[col + 1] = widthTable[col];
        }

        for ( j = 0; j < mesh->height; j++ )
        {
            LerpDrawVert( &expand[j][col],     &expand[j][col + 1], &prev );
            LerpDrawVert( &expand[j][col + 1], &expand[j][col + 2], &next );
            LerpDrawVert( &prev, &next, &mid );

            for ( k = mesh->width - 1; k > col + 3; k-- )
            {
                expand[j][k] = expand[j][k - 2];
            }

            expand[j][col + 1] = prev;
            expand[j][col + 2] = mid;
            expand[j][col + 3] = next;
        }
    }
}


/* SubdivideMeshRow  0x004275a0 */
static void SubdivideMeshRow( Mesh_t *mesh, int row, float maxError,
                              float minLength, int *heightTable )
{
    MeshVert_t prev, next, mid;
    vec3_t     prevVec, nextVec, midVec;
    float      error;
    int        i, k;

    while ( mesh->height + 2 < MAX_EXPANDED_AXIS )
    {
        for ( i = 0; i < mesh->width; i++ )
        {
            Vec3Sub( expand[row + 1][i].xyz, expand[row][i].xyz, prevVec );
            if ( Vec3Length( prevVec ) > minLength )
            {
                break;
            }

            Vec3Sub( expand[row + 2][i].xyz, expand[row + 1][i].xyz, nextVec );
            if ( Vec3Length( nextVec ) > minLength )
            {
                break;
            }

            Vec3Sub( nextVec, prevVec, midVec );
            error = Vec3Length( midVec ) * 0.25f;
            if ( error > maxError )
            {
                break;
            }
        }

        if ( i == mesh->width )
        {
            break;
        }

        mesh->height += 2;

        if ( heightTable )
        {
            for ( k = mesh->height - 1; k > row + 3; k-- )
            {
                heightTable[k] = heightTable[k - 2];
            }
            heightTable[row + 3] = heightTable[row + 1];
            heightTable[row + 2] = heightTable[row + 1];
            heightTable[row + 1] = heightTable[row];
        }

        for ( i = 0; i < mesh->width; i++ )
        {
            LerpDrawVert( &expand[row][i],     &expand[row + 1][i], &prev );
            LerpDrawVert( &expand[row + 1][i], &expand[row + 2][i], &next );
            LerpDrawVert( &prev, &next, &mid );

            for ( k = mesh->height - 1; k > row + 3; k-- )
            {
                expand[k][i] = expand[k - 2][i];
            }

            expand[row + 1][i] = prev;
            expand[row + 2][i] = mid;
            expand[row + 3][i] = next;
        }
    }
}


/* RemoveLinearMeshColumnsRows  0x00427940 */
Mesh_t *RemoveLinearMeshColumnsRows( const Mesh_t *inMesh, int *widthTable,
                                     int *heightTable )
{
    Mesh_t out;
    vec3_t proj, dir;
    float  maxLength;
    float  len;
    int    i, j, k;

    out.width  = inMesh->width;
    out.height = inMesh->height;

    for ( i = 0; i < inMesh->width; i++ )
    {
        for ( j = 0; j < inMesh->height; j++ )
        {
            expand[j][i] = inMesh->verts[j * inMesh->width + i];
        }
    }

    for ( i = 1; i < out.width - 1; i++ )
    {
        maxLength = 0.0f;

        for ( j = 0; j < out.height; j++ )
        {
            ProjectPointOntoVector( expand[j][i].xyz, expand[j][i - 1].xyz,
                                    expand[j][i + 1].xyz, proj );
            Vec3Sub( expand[j][i].xyz, proj, dir );
            len = Vec3Length( dir );
            if ( len > maxLength )
            {
                maxLength = len;
            }
        }

        if ( maxLength < MESH_LINEAR_EPSILON )
        {
            out.width--;

            for ( j = 0; j < out.height; j++ )
            {
                for ( k = i; k < out.width; k++ )
                {
                    expand[j][k] = expand[j][k + 1];
                }
            }

            if ( widthTable )
            {
                for ( k = i; k < out.width; k++ )
                {
                    widthTable[k] = widthTable[k + 1];
                }
            }

            i--;
        }
    }

    for ( j = 1; j < out.height - 1; j++ )
    {
        maxLength = 0.0f;

        for ( i = 0; i < out.width; i++ )
        {
            ProjectPointOntoVector( expand[j][i].xyz, expand[j - 1][i].xyz,
                                    expand[j + 1][i].xyz, proj );
            Vec3Sub( expand[j][i].xyz, proj, dir );
            len = Vec3Length( dir );
            if ( len > maxLength )
            {
                maxLength = len;
            }
        }

        if ( maxLength < MESH_LINEAR_EPSILON )
        {
            out.height--;

            for ( i = 0; i < out.width; i++ )
            {
                for ( k = j; k < out.height; k++ )
                {
                    expand[k][i] = expand[k + 1][i];
                }
            }

            if ( heightTable )
            {
                for ( k = j; k < out.height; k++ )
                {
                    heightTable[k] = heightTable[k + 1];
                }
            }

            j--;
        }
    }

    out.verts = &expand[0][0];
    for ( i = 1; i < out.height; i++ )
    {
        memmove( out.verts + i * out.width, &expand[i][0],
                 out.width * sizeof( MeshVert_t ) );
    }

    return CopyMesh( &out );
}


/* LerpDrawVertAmount  0x00427da0 */
void LerpDrawVertAmount( const MeshVert_t *a, const MeshVert_t *b, float amount,
                         MeshVert_t *out )
{
    out->xyz[0] = ( b->xyz[0] - a->xyz[0] ) * amount + a->xyz[0];
    out->xyz[1] = ( b->xyz[1] - a->xyz[1] ) * amount + a->xyz[1];
    out->xyz[2] = ( b->xyz[2] - a->xyz[2] ) * amount + a->xyz[2];

    out->st[0]  = ( b->st[0]  - a->st[0]  ) * amount + a->st[0];
    out->st[1]  = ( b->st[1]  - a->st[1]  ) * amount + a->st[1];

    out->lmSt[0] = ( b->lmSt[0] - a->lmSt[0] ) * amount + a->lmSt[0];
    out->lmSt[1] = ( b->lmSt[1] - a->lmSt[1] ) * amount + a->lmSt[1];

    out->color[0] = ( byte )( a->color[0] + ( b->color[0] - a->color[0] ) * amount );
    out->color[1] = ( byte )( a->color[1] + ( b->color[1] - a->color[1] ) * amount );
    out->color[2] = ( byte )( a->color[2] + ( b->color[2] - a->color[2] ) * amount );
    out->color[3] = ( byte )( a->color[3] + ( b->color[3] - a->color[3] ) * amount );

    out->normal[0] = ( b->normal[0] - a->normal[0] ) * amount + a->normal[0];
    out->normal[1] = ( b->normal[1] - a->normal[1] ) * amount + a->normal[1];
    out->normal[2] = ( b->normal[2] - a->normal[2] ) * amount + a->normal[2];

    Vec3Normalize( out->normal );
}


/* SubdivideMeshQuads  0x00427ff0 */
Mesh_t *SubdivideMeshQuads( const Mesh_t *inMesh, float subdivSize, int maxsize,
                            int *widthTable, int *heightTable,
                            int *widthArr, int *heightArr )
{
    Mesh_t out;
    vec3_t delta;
    float  maxLength;
    float  len;
    float  frac;
    int    maxSplits;
    int    splits;
    int    at;
    int    i, j, k;

    out.width  = inMesh->width;
    out.height = inMesh->height;

    for ( i = 0; i < inMesh->width; i++ )
    {
        for ( j = 0; j < inMesh->height; j++ )
        {
            expand[j][i] = inMesh->verts[j * inMesh->width + i];
        }
    }

    if ( maxsize > MAX_EXPANDED_AXIS )
    {
        Com_Error( "SubdivideMeshQuads: maxsize > MAX_EXPANDED_AXIS" );
    }

    maxSplits = ( maxsize - inMesh->width ) / ( inMesh->width - 1 );
    at        = 0;

    for ( i = 0; i < inMesh->width - 1; i++ )
    {
        maxLength = 0.0f;

        for ( j = 0; j < out.height; j++ )
        {
            Vec3Sub( expand[j][at + 1].xyz, expand[j][at].xyz, delta );
            len = Vec3Length( delta );
            if ( len > maxLength )
            {
                maxLength = len;
            }
        }

        splits = ( int )( maxLength / subdivSize );
        if ( splits > maxSplits )
        {
            splits = maxSplits;
        }

        widthTable[i] = splits + 1;

        if ( splits > 0 )
        {
            out.width += splits;

            if ( widthArr )
            {
                for ( k = out.width - 1; k >= at + splits; k-- )
                {
                    widthArr[k] = widthArr[k - splits];
                }
                for ( k = 1; k <= splits; k++ )
                {
                    widthArr[at + k] = widthArr[at];
                }
            }

            for ( j = 0; j < out.height; j++ )
            {
                for ( k = out.width - 1; k > at + splits; k-- )
                {
                    expand[j][k] = expand[j][k - splits];
                }

                for ( k = 1; k <= splits; k++ )
                {
                    frac = ( float )k / ( float )( splits + 1 );
                    LerpDrawVertAmount( &expand[j][at],
                                        &expand[j][at + splits + 1],
                                        frac, &expand[j][at + k] );
                }
            }
        }

        at += splits + 1;
    }

    maxSplits = ( maxsize - inMesh->height ) / ( inMesh->height - 1 );
    at        = 0;

    for ( j = 0; j < inMesh->height - 1; j++ )
    {
        maxLength = 0.0f;

        for ( i = 0; i < out.width; i++ )
        {
            Vec3Sub( expand[at + 1][i].xyz, expand[at][i].xyz, delta );
            len = Vec3Length( delta );
            if ( len > maxLength )
            {
                maxLength = len;
            }
        }

        splits = ( int )( maxLength / subdivSize );
        if ( splits > maxSplits )
        {
            splits = maxSplits;
        }

        heightTable[j] = splits + 1;

        if ( splits > 0 )
        {
            out.height += splits;

            if ( heightArr )
            {
                for ( k = out.height - 1; k >= at + splits; k-- )
                {
                    heightArr[k] = heightArr[k - splits];
                }
                for ( k = 1; k <= splits; k++ )
                {
                    heightArr[at + k] = heightArr[at];
                }
            }

            for ( i = 0; i < out.width; i++ )
            {
                for ( k = out.height - 1; k > at + splits; k-- )
                {
                    expand[k][i] = expand[k - splits][i];
                }

                for ( k = 1; k <= splits; k++ )
                {
                    frac = ( float )k / ( float )( splits + 1 );
                    LerpDrawVertAmount( &expand[at][i],
                                        &expand[at + splits + 1][i],
                                        frac, &expand[at + k][i] );
                }
            }
        }

        at += splits + 1;
    }

    out.verts = &expand[0][0];
    for ( i = 1; i < out.height; i++ )
    {
        memmove( out.verts + i * out.width, &expand[i][0],
                 out.width * sizeof( MeshVert_t ) );
    }

    return CopyMesh( &out );
}


/* PrintCurve  0x00428680 */
void PrintCurve( const vec3_t *points )
{
    int i, j;

    for ( i = 0; i < 3; i++ )
    {
        for ( j = 0; j < 3; j++ )
        {
            Com_Printf( "(%5.2f %5.2f %5.2f) ",
                        points[i * 3 + j][0],
                        points[i * 3 + j][1],
                        points[i * 3 + j][2] );
        }

        Com_Printf( "\n" );
    }
}
