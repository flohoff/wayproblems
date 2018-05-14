
drop table if exists meta;

PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE meta ( key varchar, value varchar );
INSERT INTO "meta" VALUES('contact.email','f@zz.de');
INSERT INTO "meta" VALUES('contact.name','Florian Lohoff');
INSERT INTO "meta" VALUES('layer.wayproblems.geometrycolumn','geometry');
INSERT INTO "meta" VALUES('layer.wayproblems.srid','4326');

INSERT INTO "meta" VALUES('layer.wayproblems.styles.default.color','#ff0000');
INSERT INTO "meta" VALUES('layer.wayproblems.styles.default.weight','4');
INSERT INTO "meta" VALUES('layer.wayproblems.styles.default.opacity','0.9');

INSERT INTO "meta" VALUES('layer.wayproblems.styles.ref.color','#000080');
INSERT INTO "meta" VALUES('layer.wayproblems.styles.ref.weight','3');
INSERT INTO "meta" VALUES('layer.wayproblems.styles.ref.opacity','0.9');

INSERT INTO "meta" VALUES('layer.wayproblems.styles.footway.color','#707000');
INSERT INTO "meta" VALUES('layer.wayproblems.styles.footway.weight','3');
INSERT INTO "meta" VALUES('layer.wayproblems.styles.footway.opacity','0.9');

INSERT INTO "meta" VALUES('layer.wayproblems.styles.redundant.color','#007070');
INSERT INTO "meta" VALUES('layer.wayproblems.styles.redundant.weight','3');
INSERT INTO "meta" VALUES('layer.wayproblems.styles.redundant.opacity','0.6');

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

INSERT INTO "meta" VALUES('layer.ref.styles.default.color','#ff0000');
INSERT INTO "meta" VALUES('layer.ref.styles.default.weight','4');
INSERT INTO "meta" VALUES('layer.ref.styles.default.opacity','0.9');

INSERT INTO "meta" VALUES('layer.ref.styles.ref.color','#000080');
INSERT INTO "meta" VALUES('layer.ref.styles.ref.weight','3');
INSERT INTO "meta" VALUES('layer.ref.styles.ref.opacity','0.9');

INSERT INTO "meta" VALUES('layer.ref.styles.footway.color','#707000');
INSERT INTO "meta" VALUES('layer.ref.styles.footway.weight','3');
INSERT INTO "meta" VALUES('layer.ref.styles.footway.opacity','0.9');

INSERT INTO "meta" VALUES('layer.ref.styles.redundant.color','#007070');
INSERT INTO "meta" VALUES('layer.ref.styles.redundant.weight','3');
INSERT INTO "meta" VALUES('layer.ref.styles.redundant.opacity','0.6');

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

INSERT INTO "meta" VALUES('layer.footway.styles.default.color','#ff0000');
INSERT INTO "meta" VALUES('layer.footway.styles.default.weight','4');
INSERT INTO "meta" VALUES('layer.footway.styles.default.opacity','0.9');

INSERT INTO "meta" VALUES('layer.footway.styles.ref.color','#000080');
INSERT INTO "meta" VALUES('layer.footway.styles.ref.weight','3');
INSERT INTO "meta" VALUES('layer.footway.styles.ref.opacity','0.9');

INSERT INTO "meta" VALUES('layer.footway.styles.footway.color','#707000');
INSERT INTO "meta" VALUES('layer.footway.styles.footway.weight','3');
INSERT INTO "meta" VALUES('layer.footway.styles.footway.opacity','0.9');

INSERT INTO "meta" VALUES('layer.footway.styles.redundant.color','#007070');
INSERT INTO "meta" VALUES('layer.footway.styles.redundant.weight','3');
INSERT INTO "meta" VALUES('layer.footway.styles.redundant.opacity','0.6');

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

INSERT INTO "meta" VALUES('layer.default.geometrycolumn','geometry');
INSERT INTO "meta" VALUES('layer.default.srid','4326');

INSERT INTO "meta" VALUES('layer.default.styles.default.color','#ff0000');
INSERT INTO "meta" VALUES('layer.default.styles.default.weight','4');
INSERT INTO "meta" VALUES('layer.default.styles.default.opacity','0.9');

INSERT INTO "meta" VALUES('layer.default.styles.ref.color','#000080');
INSERT INTO "meta" VALUES('layer.default.styles.ref.weight','3');
INSERT INTO "meta" VALUES('layer.default.styles.ref.opacity','0.9');

INSERT INTO "meta" VALUES('layer.default.styles.footway.color','#707000');
INSERT INTO "meta" VALUES('layer.default.styles.footway.weight','3');
INSERT INTO "meta" VALUES('layer.default.styles.footway.opacity','0.9');

INSERT INTO "meta" VALUES('layer.default.styles.redundant.color','#007070');
INSERT INTO "meta" VALUES('layer.default.styles.redundant.weight','3');
INSERT INTO "meta" VALUES('layer.default.styles.redundant.opacity','0.6');

INSERT INTO "meta" VALUES('layer.default.stylecolumn','style');
INSERT INTO "meta" VALUES('layer.default.columns:0','id');
INSERT INTO "meta" VALUES('layer.default.columns:1','key');
INSERT INTO "meta" VALUES('layer.default.columns:2','value');
INSERT INTO "meta" VALUES('layer.default.columns:3','changeset');
INSERT INTO "meta" VALUES('layer.default.columns:4','user');
INSERT INTO "meta" VALUES('layer.default.columns:5','timestamp');
INSERT INTO "meta" VALUES('layer.default.columns:6','problem');
INSERT INTO "meta" VALUES('layer.default.columns:7','style');
INSERT INTO "meta" VALUES('layer.default.popup', ( readfile('wayproblems-meta.popup' )) );

COMMIT;
