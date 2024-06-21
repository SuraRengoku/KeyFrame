#include "mesh.h"
#include <QtConcurrentRun>
#include <QFile>

Mesh::Mesh() {}

void Mesh::load(const QString &fn){
    reset();
    maybeRunning = true;
    /**
     * @brief high-level multithread based on available threads in thread pool
     * @param function/lambda
    */
    future=QtConcurrent::run([fn](){
        MeshData md;
        QFile infile(fn);
        if(!infile.open(QIODevice::ReadOnly)){
            qWarning("Failed to open %s", qPrintable(fn));
            return md;
        }
        QByteArray buf = infile.readAll();
        const char *p = buf.constData();
        quint32 format;
        /**
         * @brief copy n bytes content start from source to destin
         * @return void *destin
         * @param void *destin
         * @param void *source
         * @param usigned n
        */
        memcpy(&format,p,4);
        if(format != 1){
            qWarning("Invalid format in %s", qPrintable(fn));
            return md;
        }
        int ofs = 4;//offset
        memcpy(&md.vertexCount, p+ofs, 4);
        ofs += 4;
        memcpy(md.aabb,p+ofs,6 * 4);
        ofs += 6 * 4;
        const int byteCount = md.vertexCount * 8 * 4;
        md.geom.resize(byteCount);//geom:x,y,z,u,v,nx,ny,nz
        memcpy(md.geom.data(), p + ofs, byteCount);
        return md;
    });
}

MeshData *Mesh::data(){
    //future.result() will block current thread until the function bind on it is completed
    if(maybeRunning &&! meshData.isValid()) meshData = future.result();
    return &meshData;
}

void Mesh::reset(){
    //data() return the pointer of meshData, * dereference this pointer
    //MeshData() create a empty data and assign it to meshData
    *data() = MeshData();
    maybeRunning = false;
}
