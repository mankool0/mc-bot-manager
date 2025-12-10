#pragma once

#include <QObject>
#include "bot/WorldData.h"

class ChunkSavingWorker : public QObject {
    Q_OBJECT
    
public:
    explicit ChunkSavingWorker(QObject *parent = nullptr);
    ~ChunkSavingWorker();
    
public slots:
    void processChunk(const ChunkData& chunk, const QString& worldPath, int dataVersion);
};
