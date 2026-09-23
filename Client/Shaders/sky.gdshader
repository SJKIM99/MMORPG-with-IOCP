shader_type sky;

// 하늘 그라데이션 + 태양 + 흘러가는 구름.
// ProceduralSkyMaterial 에는 구름이 없어서 직접 쓴다.
// 값 잡음 FBM 두 겹이고 텍스처를 쓰지 않는다 — 에셋을 만들 수 없기 때문이다.

group_uniforms Sky;
uniform vec3 zenith_color : source_color = vec3(0.16, 0.38, 0.72);
uniform vec3 horizon_color : source_color = vec3(0.72, 0.84, 0.92);
// 지평선 아래. 내려다보는 시점에서 화면을 다 덮으므로 너무 어둡게 두면 안 된다.
uniform vec3 ground_color : source_color = vec3(0.50, 0.56, 0.52);

group_uniforms Sun;
// 태양을 향하는 단위벡터. Main.cs 가 DirectionalLight3D 의 실제 방향에서 계산해 넣는다.
//
// LIGHT0_DIRECTION 을 쓰면 LIGHT0_ENABLED 가 false 라 태양이 아예 그려지지 않았다.
// 광원 노드에서 직접 받아오면 조건 없이 항상 맞는다.
uniform vec3 sun_direction = vec3(-0.621, 0.616, -0.485);
uniform vec3 sun_color : source_color = vec3(1.0, 0.96, 0.86);
uniform float sun_size : hint_range(0.0005, 0.2) = 0.022;
uniform float sun_glow : hint_range(1.0, 128.0) = 18.0;
uniform float sun_energy : hint_range(0.0, 40.0) = 12.0;

group_uniforms Clouds;
uniform vec3 cloud_color : source_color = vec3(1.0, 0.99, 0.97);
uniform vec3 cloud_shadow : source_color = vec3(0.55, 0.62, 0.72);
uniform float cloud_coverage : hint_range(0.0, 1.0) = 0.46;
uniform float cloud_sharpness : hint_range(0.5, 12.0) = 3.2;
uniform float cloud_speed = 0.006;
uniform float cloud_scale = 2.4;

// --- 값 잡음 ---------------------------------------------------------------

float hash21(vec2 p) {
	p = fract(p * vec2(233.34, 851.73));
	p += dot(p, p + 23.45);
	return fract(p.x * p.y);
}

float value_noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);       // smoothstep 보간
	float a = hash21(i);
	float b = hash21(i + vec2(1.0, 0.0));
	float c = hash21(i + vec2(0.0, 1.0));
	float d = hash21(i + vec2(1.0, 1.0));
	return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
	float sum = 0.0;
	float amp = 0.5;
	for (int i = 0; i < 5; i++) {
		sum += value_noise(p) * amp;
		p = p * 2.03 + vec2(17.3, 9.1);  // 무리수에 가까운 비율로 반복 무늬를 깬다
		amp *= 0.5;
	}
	return sum;
}

void sky() {
	vec3 dir = normalize(EYEDIR);

	// --- 바탕 그라데이션 ---
	float up = dir.y;
	float t = pow(clamp(up, 0.0, 1.0), 0.45);
	vec3 col = mix(horizon_color, zenith_color, t);
	// 지평선 아래는 지면 색으로 떨어뜨린다.
	col = mix(col, ground_color, smoothstep(0.0, -0.12, up));

	// --- 구름 ---
	// 하늘을 평면에 투영한다. 지평선 근처에서 발산하므로 y 를 붙들어 둔다.
	if (up > 0.0) {
		vec2 plane = dir.xz / max(up, 0.10) * cloud_scale;
		vec2 drift = vec2(TIME * cloud_speed, TIME * cloud_speed * 0.42);

		float n = fbm(plane + drift);
		// 두 번째 겹으로 가장자리를 헝클어 뭉게구름처럼 만든다.
		n = mix(n, fbm(plane * 2.7 - drift * 1.7), 0.35);

		float density = smoothstep(1.0 - cloud_coverage, 1.0 - cloud_coverage + 0.22, n);
		density = pow(density, cloud_sharpness / 3.2);
		// 지평선 쪽은 서서히 걷어낸다 — 그러지 않으면 띠처럼 뭉친다.
		density *= smoothstep(0.02, 0.28, up);

		// 위쪽이 밝고 아래가 어두운 간이 음영.
		float shade = smoothstep(0.25, 0.85, n);
		vec3 cloud = mix(cloud_shadow, cloud_color, shade);

		col = mix(col, cloud, clamp(density, 0.0, 1.0));
	}

	// --- 태양 ---
	float d = dot(dir, normalize(sun_direction));
	float disk = smoothstep(1.0 - sun_size, 1.0 - sun_size * 0.25, d);
	float glow = pow(clamp(d, 0.0, 1.0), sun_glow);
	// 구름 뒤로 들어가면 가려진다 — 태양 주변이 그냥 밝기만 하면 붙여넣은 티가 난다.
	col += sun_color * (disk * sun_energy + glow * 0.7);

	COLOR = col;
}
