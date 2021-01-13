#version 120

#include "common/shadows_fragment.h"

uniform sampler2D baseTex;
uniform sampler2D losTex;

uniform vec3 shadingColor;
uniform vec3 ambient;
uniform vec3 sunColor;
uniform vec3 sunDir;
uniform vec3 cameraPos;

uniform float specularPower;
uniform vec3 specularColor;

varying vec4 v_tex;
varying vec4 v_shadow;
varying vec2 v_los;
varying vec3 v_half;
varying vec3 v_normal;
varying float v_transp;
varying vec3 v_lighting;

void main()
{
	//vec4 texdiffuse = textureGrad(baseTex, vec3(fract(v_tex.xy), v_tex.z), dFdx(v_tex.xy), dFdy(v_tex.xy));
	vec4 texdiffuse = texture2D(baseTex, fract(v_tex.xy));

	if (texdiffuse.a < 0.25)
		discard;

	texdiffuse.a *= v_transp;

	vec3 specular = sunColor * specularColor * pow(max(0.0, dot(normalize(v_normal), v_half)), specularPower);

	vec3 color = (texdiffuse.rgb * v_lighting + specular) * get_shadow();
        color += texdiffuse.rgb * ambient;

	#if !IGNORE_LOS
		float los = texture2D(losTex, v_los).a;
		los = los < 0.03 ? 0.0 : los;
		color *= los;
	#endif

	gl_FragColor.rgb = color;
	gl_FragColor.a = texdiffuse.a;
}

