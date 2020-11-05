#ifndef ANNOTATION_H
#define ANNOTATION_H

#include "ndlib/process/process.h"

class ProcessSpawnInfo;

class Annotation : public Process
{
public:
	STATE_DECLARE_OVERRIDE(Active);

	virtual Err Init(const ProcessSpawnInfo& info) override;	
	Point m_pos;
	float m_drawDistance;
	const char* m_text;
	
};

#endif //ANNOTATION_H
