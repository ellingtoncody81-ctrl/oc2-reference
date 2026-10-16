#ifndef OC2_VIEWMODEL_TRANSITION_H
#define OC2_VIEWMODEL_TRANSITION_H

#include <stdint.h>
#include <math.h>

// Source reference: sequence_Transitioner.cpp and C_AnimationLayer::GetFadeout.
// GoldSrc has no per-sequence fadein/fadeout metadata; use a bounded 0.2s
// presentation adapter. Outgoing clips keep advancing, including repeated
// sequences. This is a sequence queue, not a low-pass filter on bone matrices.
namespace oc2_viewmodel
{
inline bool Finite(double x) { return x-x == 0; }
inline float Fade(double elapsed, float duration)
{
	if (!Finite(elapsed) || duration <= 0) return 0;
	float s = 1.0f-static_cast<float>(elapsed/duration);
	if (s <= 0) return 0;
	if (s >= 1) return 1;
	return s*s*(3.0f-2.0f*s);
}
inline float Frame(float value, int frames, bool loop)
{
	if (!Finite(value) || frames <= 1) return 0;
	const float last = static_cast<float>(frames-1);
	if (loop)
	{
		value = fmodf(value,last);
		return value < 0 ? value+last : value;
	}
	if (value < 0) return 0;
	return value < last-.001f ? value : last-.001f;
}
struct Layer
{
	int sequence = 0, frames = 1;
	float frame = 0, fps = 0, rate = 1, duration = .2f;
	int blend0 = 0, blend1 = 0;
	bool loop = false;
	double sampleTime = 0, startTime = 0, fadeStart = 0;
};
class Transitioner
{
public:
	enum Change { None, First, Model, Sequence, Restart, Clock };
	void Clear() { m_ready = false; m_count = 0; m_key = 0; }
	Change Observe(uintptr_t key, const Layer &next, bool interpolate)
	{
		if (!Finite(next.sampleTime) || !Finite(next.startTime) || !Finite(next.frame))
			{ Clear(); return Clock; }
		Change changed = !m_ready ? First : key != m_key ? Model :
			next.sampleTime < m_current.sampleTime || next.sampleTime-m_current.sampleTime > .5 ? Clock :
			next.sequence != m_current.sequence ? Sequence : next.startTime != m_current.startTime ? Restart : None;
		if (changed == First || changed == Model || changed == Clock || !interpolate)
			m_count = 0;
		else if (changed != None)
		{
			if (m_count == 2) { m_old[0] = m_old[1]; m_count = 1; }
			Layer old = m_current;
			old.fadeStart = next.sampleTime;
			old.duration = .2f;
			m_old[m_count++] = old;
		}
		m_current = next; m_key = key; m_ready = true;
		for (int i=0; i<m_count;)
		{
			if (Fade(next.sampleTime-m_old[i].fadeStart,m_old[i].duration) > 0) { ++i; continue; }
			for (int j=i; j+1<m_count; ++j) m_old[j] = m_old[j+1];
			--m_count;
		}
		return changed;
	}
	int Count() const { return m_count; }
	const Layer &Previous(int index) const { return m_old[index]; }
	float PreviousFrame(int index, double time) const
	{
		const Layer &p=m_old[index];
		return Frame(p.frame+static_cast<float>(time-p.sampleTime)*p.rate*p.fps,p.frames,p.loop);
	}
	float Weight(int index, double time) const { return Fade(time-m_old[index].fadeStart,m_old[index].duration); }
private:
	bool m_ready = false;
	uintptr_t m_key = 0;
	int m_count = 0;
	Layer m_current, m_old[2];
};
}
#endif
