#ifndef MESH_H
#define MESH_H

#include <QString>
#include <QFuture>

struct MeshData{
    bool isValid() const {return vertexCount>0;}
    int vertexCount=0;
    float aabb[6];
    QByteArray geom;//x,y,z,u,v,nx,ny,nz
};

class Mesh
{
public:
    Mesh();
    void load(const QString &fn);
    MeshData *data();
    bool isValid(){return data()->isValid();}
    void reset();
private:
    bool maybeRunning=false;
    QFuture<MeshData> future;
    MeshData meshData;
};

#endif // MESH_H
