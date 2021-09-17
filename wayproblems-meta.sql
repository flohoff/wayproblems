
drop table if exists meta;

PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE meta ( key varchar, value varchar );
INSERT INTO "meta" VALUES('contact.email','f@zz.de');
INSERT INTO "meta" VALUES('contact.name','Florian Lohoff');


INSERT INTO "meta" VALUES('style.default.color','#ff0000');
INSERT INTO "meta" VALUES('style.default.weight','4');
INSERT INTO "meta" VALUES('style.default.opacity','0.9');

INSERT INTO "meta" VALUES('style.ref.color','#000080');
INSERT INTO "meta" VALUES('style.ref.weight','3');
INSERT INTO "meta" VALUES('style.ref.opacity','0.9');

INSERT INTO "meta" VALUES('style.footway.color','#707000');
INSERT INTO "meta" VALUES('style.footway.weight','3');
INSERT INTO "meta" VALUES('style.footway.opacity','0.9');

INSERT INTO "meta" VALUES('style.redundant.color','#007070');
INSERT INTO "meta" VALUES('style.redundant.weight','3');
INSERT INTO "meta" VALUES('style.redundant.opacity','0.6');

INSERT INTO "meta" VALUES('style.redline.color','#ff0000');
INSERT INTO "meta" VALUES('style.redline.weight','4');
INSERT INTO "meta" VALUES('style.redline.opacity','0.9');

INSERT INTO "meta" VALUES('style.steelline.color','#374e66');
INSERT INTO "meta" VALUES('style.steelline.weight','4');
INSERT INTO "meta" VALUES('style.steelline.opacity','0.9');

INSERT INTO "meta" VALUES('style.brownline.color','#CD853F');
INSERT INTO "meta" VALUES('style.brownline.weight','4');
INSERT INTO "meta" VALUES('style.brownline.opacity','0.9');

INSERT INTO "meta" VALUES('style.violetline.color','#9932CC');
INSERT INTO "meta" VALUES('style.violetline.weight','4');
INSERT INTO "meta" VALUES('style.violetline.opacity','0.9');

INSERT INTO "meta" VALUES('layer.wayproblems.geometrycolumn','geometry');
INSERT INTO "meta" VALUES('layer.wayproblems.srid','4326');
INSERT INTO "meta" VALUES('layer.wayproblems.stylecolumn','style');
INSERT INTO "meta" VALUES('layer.wayproblems.columns:0','id');
INSERT INTO "meta" VALUES('layer.wayproblems.columns:1','key');
INSERT INTO "meta" VALUES('layer.wayproblems.columns:2','value');
INSERT INTO "meta" VALUES('layer.wayproblems.columns:3','changeset');
INSERT INTO "meta" VALUES('layer.wayproblems.columns:4','user');
INSERT INTO "meta" VALUES('layer.wayproblems.columns:5','timestamp');
INSERT INTO "meta" VALUES('layer.wayproblems.columns:6','problem');
INSERT INTO "meta" VALUES('layer.wayproblems.columns:7','style');
INSERT INTO "meta" VALUES('layer.wayproblems.popup', ( readfile('wayproblems-meta.popup' )) );



INSERT INTO "meta" VALUES('layer.ref.geometrycolumn','geometry');
INSERT INTO "meta" VALUES('layer.ref.srid','4326');
INSERT INTO "meta" VALUES('layer.ref.stylecolumn','style');
INSERT INTO "meta" VALUES('layer.ref.columns:0','id');
INSERT INTO "meta" VALUES('layer.ref.columns:1','key');
INSERT INTO "meta" VALUES('layer.ref.columns:2','value');
INSERT INTO "meta" VALUES('layer.ref.columns:3','changeset');
INSERT INTO "meta" VALUES('layer.ref.columns:4','user');
INSERT INTO "meta" VALUES('layer.ref.columns:5','timestamp');
INSERT INTO "meta" VALUES('layer.ref.columns:6','problem');
INSERT INTO "meta" VALUES('layer.ref.columns:7','style');
INSERT INTO "meta" VALUES('layer.ref.popup', ( readfile('wayproblems-meta.popup' )) );




INSERT INTO "meta" VALUES('layer.footway.geometrycolumn','geometry');
INSERT INTO "meta" VALUES('layer.footway.srid','4326');
INSERT INTO "meta" VALUES('layer.footway.stylecolumn','style');
INSERT INTO "meta" VALUES('layer.footway.columns:0','id');
INSERT INTO "meta" VALUES('layer.footway.columns:1','key');
INSERT INTO "meta" VALUES('layer.footway.columns:2','value');
INSERT INTO "meta" VALUES('layer.footway.columns:3','changeset');
INSERT INTO "meta" VALUES('layer.footway.columns:4','user');
INSERT INTO "meta" VALUES('layer.footway.columns:5','timestamp');
INSERT INTO "meta" VALUES('layer.footway.columns:6','problem');
INSERT INTO "meta" VALUES('layer.footway.columns:7','style');
INSERT INTO "meta" VALUES('layer.footway.popup', ( readfile('wayproblems-meta.popup' )) );


INSERT INTO "meta" VALUES('layer.strange.geometrycolumn','geometry');
INSERT INTO "meta" VALUES('layer.strange.srid','4326');
INSERT INTO "meta" VALUES('layer.strange.stylecolumn','style');
INSERT INTO "meta" VALUES('layer.strange.columns:0','id');
INSERT INTO "meta" VALUES('layer.strange.columns:1','key');
INSERT INTO "meta" VALUES('layer.strange.columns:2','value');
INSERT INTO "meta" VALUES('layer.strange.columns:3','changeset');
INSERT INTO "meta" VALUES('layer.strange.columns:4','user');
INSERT INTO "meta" VALUES('layer.strange.columns:5','timestamp');
INSERT INTO "meta" VALUES('layer.strange.columns:6','problem');
INSERT INTO "meta" VALUES('layer.strange.columns:7','style');
INSERT INTO "meta" VALUES('layer.strange.popup', ( readfile('wayproblems-meta.popup' )) );



INSERT INTO "meta" VALUES('layer.defaults.geometrycolumn','geometry');
INSERT INTO "meta" VALUES('layer.defaults.srid','4326');
INSERT INTO "meta" VALUES('layer.defaults.stylecolumn','style');
INSERT INTO "meta" VALUES('layer.defaults.columns:0','id');
INSERT INTO "meta" VALUES('layer.defaults.columns:1','key');
INSERT INTO "meta" VALUES('layer.defaults.columns:2','value');
INSERT INTO "meta" VALUES('layer.defaults.columns:3','changeset');
INSERT INTO "meta" VALUES('layer.defaults.columns:4','user');
INSERT INTO "meta" VALUES('layer.defaults.columns:5','timestamp');
INSERT INTO "meta" VALUES('layer.defaults.columns:6','problem');
INSERT INTO "meta" VALUES('layer.defaults.columns:7','style');
INSERT INTO "meta" VALUES('layer.defaults.popup', ( readfile('wayproblems-meta.popup' )) );



COMMIT;
