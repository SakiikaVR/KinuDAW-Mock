// Two cached GPU meshes per channel. Playback never mutates wave DOM/layout.
class TransportWave final : public Rml::Element {
public:
	explicit TransportWave(const Rml::String& tag) : Rml::Element(tag) {}
protected:
	void OnRender() override
	{
		const Rml::Vector2f size = GetBox().GetSize(Rml::BoxArea::Content);
		if (size.x <= 0 || size.y <= 0) return;
		auto* renderer = GetRenderManager();
		if (size != cached_size)
		{
			cached_size = size;
			const bool right = GetId() == "transport-wave-r";
			wave = renderer->MakeGeometry(BuildMesh(size, right, false));
			flat = renderer->MakeGeometry(BuildMesh(size, right, true));
		}
		const auto origin = GetAbsoluteOffset(Rml::BoxArea::Content);
		const auto old_scissor = renderer->GetScissorRegion();
		const auto bounds = Rml::Rectanglei::FromPositionSize(Rml::Vector2i(int(origin.x), int(origin.y)), Rml::Vector2i(int(size.x), int(size.y)));
		renderer->SetScissorRegion(bounds.IntersectIfValid(old_scissor));
		const float offset = playing ? std::fmod(playhead_beat * size.x * .05f, size.x) : 0.f;
		(playing ? wave : flat).Render(origin - Rml::Vector2f(offset, 0.f));
		if (old_scissor.Valid()) renderer->SetScissorRegion(old_scissor);
		else renderer->DisableScissorRegion();
	}
private:
	Rml::Vector2f cached_size;
	Rml::Geometry wave, flat;
	static Rml::Mesh BuildMesh(Rml::Vector2f size, bool right, bool stopped)
	{
		Rml::Mesh mesh;
		const int count = stopped ? 1 : 128;
		mesh.vertices.reserve(count * 4); mesh.indices.reserve(count * 6);
		auto sample = [&](float x) {
			const float phase = x / size.x * 6.2831853f;
			return size.y * .5f + (stopped ? 0.f : size.y * (.27f * std::sin(phase * 5.f + (right ? .8f : 0.f)) + .10f * std::sin(phase * 12.f)));
		};
		for (int i = 0; i < count; ++i)
		{
			const float x0 = size.x * 2.f * i / count, x1 = size.x * 2.f * (i + 1) / count;
			const float y0 = sample(x0), y1 = sample(x1);
			const float dx = x1 - x0, dy = y1 - y0;
			const float length = std::sqrt(dx * dx + dy * dy);
			const float nx = -dy / length * .65f, ny = dx / length * .65f;
			const int base = int(mesh.vertices.size());
			const Rml::ColourbPremultiplied color(101, 201, 165, 255);
			for (Rml::Vector2f point : {Rml::Vector2f(x0 + nx, y0 + ny), Rml::Vector2f(x1 + nx, y1 + ny), Rml::Vector2f(x1 - nx, y1 - ny), Rml::Vector2f(x0 - nx, y0 - ny)})
				mesh.vertices.push_back({point, color, {0, 0}});
			for (int index : {0, 1, 2, 0, 2, 3}) mesh.indices.push_back(base + index);
		}
		return mesh;
	}
};
